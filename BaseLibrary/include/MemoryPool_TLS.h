#pragma once

#include "LockFree_Stack.h"
#include "MemoryPool_LF.h"

template <typename T>
class MemoryPool_TLS_Chunck {
public:
    static constexpr int kChunkDefault = 100;

    void* AllocChunck() {
        void* chunck;
        if (ChunckStack.Pop(chunck) == false) {
            InterlockedExchangeAdd(&TotalSize, kChunkDefault);
            InterlockedExchangeAdd(&UseSize, kChunkDefault);
            chunck = NULL;
        } else {
            InterlockedExchangeAdd(&FreeSize, -kChunkDefault);
            InterlockedExchangeAdd(&UseSize, kChunkDefault);
        }
        return chunck;
    }

    void FreeChunck(void* chunck) {
        int ChunckSize = kChunkDefault;
        InterlockedExchangeAdd(&UseSize, -kChunkDefault);
        InterlockedExchangeAdd(&FreeSize, kChunkDefault);
        ChunckStack.Push(chunck);
    }


    static MemoryPool_TLS_Chunck* GetInstance(void) {
        if (pChunckPool == NULL) {
            Lock();
            if (pChunckPool == NULL) {
#pragma warning(push)
#pragma warning(disable : 4316)  // Phase 3 C++17 진입(/Zc:alignedNew) 시 제거
                pChunckPool = new MemoryPool_TLS_Chunck;
#pragma warning(pop)
                atexit(Destroy);
            }
            Unlock();
        }

        return pChunckPool;
    }

    DWORD GetTLSIndex(void) {
        return TLS_idx;
    }
    int GetChunckSize(void) {
        return kChunkDefault;
    }

    int GetTotalMemCnt(void) {
        return TotalSize;
    }
    int GetUseMemCnt(void) {
        return UseSize;
    }
    int GetFreeMemCnt(void) {
        return FreeSize;
    }


    int GetStackSize(void) {
        return ChunckStack.GetUseSize();
    }
    int GetPoolCnt_Total(void) {
        return ChunckStack.GetTotalPool();
    }
    int GetPoolCnt_Use(void) {
        return ChunckStack.GetUsePool();
    }
    int GetPoolCnt_Free(void) {
        return ChunckStack.GetFreePool();
    }

private:
    MemoryPool_TLS_Chunck() {
        TLS_idx = TlsAlloc();

        TotalSize = 0;
        UseSize = 0;
        FreeSize = 0;
    }
    ~MemoryPool_TLS_Chunck() {
        TlsFree(TLS_idx);
    }

    static void Lock() {
        while (InterlockedExchange(&Key_Singleton, 1) != 0)
            ;
    }
    static void Unlock() {
        Key_Singleton = 0;
    }

    static void Destroy(void) {
        delete pChunckPool;
        pChunckPool = NULL;
    }

private:
    LockFree_Stack<void*> ChunckStack;
    DWORD TLS_idx;

    long TotalSize;
    long UseSize;
    long FreeSize;

    static long Key_Singleton;
    static MemoryPool_TLS_Chunck* pChunckPool;
};

template <typename T>
MemoryPool_TLS_Chunck<T>* MemoryPool_TLS_Chunck<T>::pChunckPool = NULL;

template <typename T>
long MemoryPool_TLS_Chunck<T>::Key_Singleton = 0;

//////////////////////////////////////////////////////////////////////


template <typename T>
class MemoryPool_TLS_Node {
public:
    using PadType = unsigned __int64;
    static constexpr PadType kPadUnderflow = 0xdfdfdfdfdfdfdfdf;
    static constexpr PadType kPadOverflow = 0xefefefefefefefef;

    struct stNode {
        PadType Pad_UnderFlow = kPadUnderflow;
        T Data;
        stNode* Next = NULL;
        PadType Pad_OverFlow = kPadOverflow;
    };

    MemoryPool_TLS_Node() {
        ChunckPool = MemoryPool_TLS_Chunck<T>::GetInstance();
        Head = NULL;
        FreeNodeCnt = 0;
        ChunckSize = ChunckPool->GetChunckSize();
    }

    ~MemoryPool_TLS_Node() {
        while (Head != NULL) {
            stNode* Next = Head->Next;
            delete Head;
            Head = Next;
        }
        FreeNodeCnt = 0;
    }

    void SetTLS(void) { TlsSetValue(ChunckPool->GetTLSIndex(), this); }

    T* Alloc() {
        if (FreeNodeCnt == 0) {
            Head = (stNode*)ChunckPool->AllocChunck();
            if (Head == NULL)
                Head = NewNodeList();
            FreeNodeCnt += ChunckSize;
        }

        stNode* node = Head;
        Head = Head->Next;
        FreeNodeCnt--;

        return &node->Data;
    }

    void Free(T* target) {
        stNode* node = (stNode*)((char*)target - sizeof(PadType));
        if (node->Pad_UnderFlow != kPadUnderflow ||
            node->Pad_OverFlow != kPadOverflow) {
            char* Err_Make = NULL;
            *Err_Make = 10;
        }

        node->Next = Head;
        Head = node;
        if (++FreeNodeCnt >= ChunckSize) {
            for (int i = 0; i < ChunckSize; i++)
                Head = Head->Next;
            FreeNodeCnt -= ChunckSize;
            ChunckPool->FreeChunck(node);
        }
    }

private:
    stNode* NewNodeList() {
        stNode* head = Head;

        for (int i = 0; i < ChunckSize; i++) {
            stNode* newNode = new stNode;

            newNode->Next = head;
            head = newNode;
        }

        return head;
    }

private:
    MemoryPool_TLS_Chunck<T>* ChunckPool;

    stNode* Head;

    int FreeNodeCnt;
    int ChunckSize;
};