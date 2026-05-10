#pragma once
#include <windows.h>

#include "MemoryPool_TLS.h"
//#define LFQ_DEBUG

template <typename T>
class LockFree_Queue_TLS {
public:
    static constexpr unsigned __int64 kMaxMemoryRange = 0x00007ffffffeffff;

private:
    struct stNode {
        T Data = NULL;
        stNode* Next = NULL;
        short RefCnt = 0;
        short DeleteFlag = 0;

        void IncrementRef(void) { InterlockedIncrement16(&RefCnt); }
        short DecrementRef(void) { return InterlockedDecrement16(&RefCnt); }
    };

    struct BitField {
        unsigned __int64 Index : 47;
        unsigned __int64 Flag : 1;
        unsigned __int64 Tag : 16;
    };

    union stNode_TAGED {
        unsigned __int64 Data;
        BitField Bit;

        stNode_TAGED() { Data = 0; }

        inline void SetTag(WORD Tag) { Bit.Tag = Tag; }

        inline WORD GetTag() { return Bit.Tag; }
        inline stNode* GetPtr() { return (stNode*)Bit.Index; }
        inline bool CAS(unsigned __int64 Swap, unsigned __int64 Comp) {
            PVOID retval =
                InterlockedCompareExchangePointer((PVOID*)&Data, (PVOID)Swap, (PVOID)Comp);
            return retval == (PVOID)Comp;
        }

        inline bool CAS_Next(unsigned __int64 Swap, unsigned __int64 Comp) {
            PVOID retval = InterlockedCompareExchangePointer((PVOID*)&((stNode*)Bit.Index)->Next,
                                                             (PVOID)Swap, (PVOID)Comp);
            return retval == (PVOID)Comp;
        }

        stNode_TAGED(stNode* node) { Bit.Index = (unsigned __int64)node; }
        inline void operator=(stNode* node) { Bit.Index = (unsigned __int64)node; }
        inline stNode* operator->(void) { return (stNode*)Bit.Index; }
        inline bool operator==(stNode_TAGED Comp) { return Data == Comp.Data; }
    };

public:
    LockFree_Queue_TLS(void);

    void Enqueue(T data);
    bool Dequeue(T& tData);

    void Clear(void);

    int GetUseSize(void) { return Size; }

    ////////////////////////////////////////////
    static int Log_GetTotalMemCnt(void) {
        return MemoryPool_TLS_Chunck<stNode>::GetInstance()->Log_GetTotalMemCnt();
    }

    static int GetStackSize(void) {
        return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetStackSize();
    }
    static int GetPool_TotalSize(void) {
        return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Total();
    }
    static int GetPool_UseSize(void) {
        return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Use();
    }
    static int GetPool_FreeSize(void) {
        return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Free();
    }


private:
    inline WORD GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }
    inline stNode* AllocNode(void) {
        MemoryPool_TLS_Node<stNode>* nodePool = (MemoryPool_TLS_Node<stNode>*)TlsGetValue(
            MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetTLSIndex());
        if (nodePool == NULL) {
            nodePool = new MemoryPool_TLS_Node<stNode>;
            nodePool->SetTLS();
        }

        return nodePool->Alloc();
    }

    inline void FreeNode(stNode* node) {
        MemoryPool_TLS_Node<stNode>* nodePool = (MemoryPool_TLS_Node<stNode>*)TlsGetValue(
            MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetTLSIndex());
        if (nodePool == NULL) {
            nodePool = new MemoryPool_TLS_Node<stNode>;
            nodePool->SetTLS();
        }
        nodePool->Free(node);
    }

private:
    stNode_TAGED Head;
    stNode_TAGED Tail;

    long Size;
    short TagCnt;
};


template <typename T>
LockFree_Queue_TLS<T>::LockFree_Queue_TLS(void) {
    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    if (SystemInfo.lpMaximumApplicationAddress != (void*)kMaxMemoryRange) {
        wprintf(L"LockFree_Queue_TLS: user-space memory range mismatch (47-bit pointer encoding may break)\n");
        return;
    }

    Size = 0;
    TagCnt = 0;

    /*Head = NodePool.Alloc();*/
    Head = AllocNode();
    Head->Next = NULL;
    Head->IncrementRef();
    Tail = Head;
}

template <typename T>
void LockFree_Queue_TLS<T>::Enqueue(T data) {
    stNode_TAGED OldTail;
    stNode_TAGED Next;
    stNode_TAGED NewNode = AllocNode();
    NewNode->Data = data;
    NewNode->Next = NULL;
    NewNode->IncrementRef();
    NewNode.SetTag(GetTagCnt());

    while (1) {
        OldTail = Tail;
        OldTail->IncrementRef();
        Next = Tail->Next;

        if (OldTail == Tail) {
            if (Next.Data == NULL) {
                if (OldTail.CAS_Next(NewNode.Data, Next.Data)) {
                    Tail.CAS(NewNode.Data, OldTail.Data);

                    if (OldTail->DecrementRef() == 0) {
                        if (InterlockedExchange16(&OldTail->DeleteFlag, 0) == 1)
                            FreeNode(OldTail.GetPtr()); /*NodePool.Free(OldTail.GetPtr());*/
                    }
                    break;
                }
            } else
                Tail.CAS(Next.Data, OldTail.Data);
        }

        if (OldTail->DecrementRef() == 0) {
            if (InterlockedExchange16(&OldTail->DeleteFlag, 0) == 1)
                FreeNode(OldTail.GetPtr()); /*NodePool.Free(OldTail.GetPtr());*/
        }
    }
    InterlockedExchangeAdd(&Size, 1);
}


template <typename T>
bool LockFree_Queue_TLS<T>::Dequeue(T& tData) {
    if (InterlockedExchangeAdd(&Size, -1) <= 0) {
        InterlockedExchangeAdd(&Size, 1);
        tData = NULL;
        return false;
    }

    stNode_TAGED CurHead;
    stNode_TAGED CurTail;
    stNode_TAGED Next;
    Next.SetTag(GetTagCnt());

    while (1) {
        CurHead = Head;
        CurTail = Tail;

        Next = CurHead->Next;

        if (CurHead == Head) {
            if (CurHead.GetPtr() == CurTail.GetPtr())
                Tail.CAS(Next.Data, CurTail.Data);
            else {
                tData = Next->Data;
                if (Head.CAS(Next.Data, CurHead.Data)) {
                    InterlockedExchange16(&CurHead->DeleteFlag, 1);
                    if (CurHead->DecrementRef() == 0) {
                        if (InterlockedExchange16(&CurHead->DeleteFlag, 0) == 1)
                            FreeNode(CurHead.GetPtr()); /*NodePool.Free(CurHead.GetPtr());*/
                    }
                    break;
                }
            }
        }
    }

    return true;
}


template <typename T>
void LockFree_Queue_TLS<T>::Clear(void) {
    stNode_TAGED tmpHead;
    stNode_TAGED Next;

    while (Size) {
        tmpHead = Head;
        Next = tmpHead->Next;

        if (Next == NULL)
            break;

        Head = Next;
        tmpHead->RefCnt = 0;
        FreeNode(tmpHead.GetPtr());
        InterlockedExchangeAdd(&Size, -1);
    }
}