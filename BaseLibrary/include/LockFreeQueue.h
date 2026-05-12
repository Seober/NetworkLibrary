#pragma once
#include <windows.h>

#include "TLSMemoryPool.h"
//#define LFQ_DEBUG

template <typename T>
class LockFreeQueue {
public:
    static constexpr unsigned __int64 kMaxMemoryRange = 0x00007ffffffeffff;

private:
    struct stNode {
        T Data = nullptr;
        stNode* Next = nullptr;
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
    LockFreeQueue(void);

    void Enqueue(T data);
    bool Dequeue(T& tData);

    void Clear(void);

    int GetUseSize(void) { return Size; }


private:
    inline WORD GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }
    inline stNode* AllocNode(void) {
        TLSNodeMemoryPool<stNode>* nodePool = (TLSNodeMemoryPool<stNode>*)TlsGetValue(
            TLSChunkMemoryPool<stNode>::GetInstance()->GetTLSIndex());
        if (nodePool == nullptr) {
            nodePool = new TLSNodeMemoryPool<stNode>;
            nodePool->SetTLS();
        }

        return nodePool->Alloc();
    }

    inline void FreeNode(stNode* node) {
        TLSNodeMemoryPool<stNode>* nodePool = (TLSNodeMemoryPool<stNode>*)TlsGetValue(
            TLSChunkMemoryPool<stNode>::GetInstance()->GetTLSIndex());
        if (nodePool == nullptr) {
            nodePool = new TLSNodeMemoryPool<stNode>;
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
LockFreeQueue<T>::LockFreeQueue(void) {
    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    if (SystemInfo.lpMaximumApplicationAddress != (void*)kMaxMemoryRange) {
        wprintf(L"LockFreeQueue: user-space memory range mismatch (47-bit pointer encoding may break)\n");
        return;
    }

    Size = 0;
    TagCnt = 0;

    /*Head = NodePool.Alloc();*/
    Head = AllocNode();
    Head->Next = nullptr;
    Head->IncrementRef();
    Tail = Head;
}

template <typename T>
void LockFreeQueue<T>::Enqueue(T data) {
    stNode_TAGED OldTail;
    stNode_TAGED Next;
    stNode_TAGED NewNode = AllocNode();
    NewNode->Data = data;
    NewNode->Next = nullptr;
    NewNode->IncrementRef();
    NewNode.SetTag(GetTagCnt());

    while (1) {
        OldTail = Tail;
        OldTail->IncrementRef();
        Next = Tail->Next;

        if (OldTail == Tail) {
            if (Next.Data == nullptr) {
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
bool LockFreeQueue<T>::Dequeue(T& tData) {
    if (InterlockedExchangeAdd(&Size, -1) <= 0) {
        InterlockedExchangeAdd(&Size, 1);
        tData = nullptr;
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
void LockFreeQueue<T>::Clear(void) {
    stNode_TAGED tmpHead;
    stNode_TAGED Next;

    while (Size) {
        tmpHead = Head;
        Next = tmpHead->Next;

        if (Next == nullptr)
            break;

        Head = Next;
        tmpHead->RefCnt = 0;
        FreeNode(tmpHead.GetPtr());
        InterlockedExchangeAdd(&Size, -1);
    }
}