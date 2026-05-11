#pragma once

#include "LockFreeStack.h"
#include "LockFreeMemoryPool.h"

template <typename T>
class TLSChunkMemoryPool {
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


    static TLSChunkMemoryPool* GetInstance(void) {
        if (pChunckPool == NULL) {
            Lock();
            if (pChunckPool == NULL) {
#pragma warning(push)
#pragma warning(disable : 4316)  // Phase 3 C++17 진입(/Zc:alignedNew) 시 제거
                pChunckPool = new TLSChunkMemoryPool;
#pragma warning(pop)
                atexit(Destroy);
            }
            Unlock();
        }

        return pChunckPool;
    }

    DWORD GetTLSIndex(void) {
        return TLSIdx;
    }
    int GetChunkSize(void) {
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

private:
    TLSChunkMemoryPool() {
        TLSIdx = TlsAlloc();

        TotalSize = 0;
        UseSize = 0;
        FreeSize = 0;
    }
    ~TLSChunkMemoryPool() {
        TlsFree(TLSIdx);
    }

    static void Lock() {
        while (InterlockedExchange(&KeySingleton, 1) != 0)
            ;
    }
    static void Unlock() {
        KeySingleton = 0;
    }

    static void Destroy(void) {
        delete pChunckPool;
        pChunckPool = NULL;
    }

private:
    LockFreeStack<void*> ChunckStack;
    DWORD TLSIdx;

    long TotalSize;
    long UseSize;
    long FreeSize;

    static long KeySingleton;
    static TLSChunkMemoryPool* pChunckPool;
};

template <typename T>
TLSChunkMemoryPool<T>* TLSChunkMemoryPool<T>::pChunckPool = NULL;

template <typename T>
long TLSChunkMemoryPool<T>::KeySingleton = 0;

//////////////////////////////////////////////////////////////////////


template <typename T>
class TLSNodeMemoryPool {
public:
    using PadType = unsigned __int64;
    static constexpr PadType kPadUnderflow = 0xdfdfdfdfdfdfdfdf;
    static constexpr PadType kPadOverflow = 0xefefefefefefefef;

    struct stNode {
        PadType PadUnderflow = kPadUnderflow;
        T Data;
        stNode* Next = NULL;
        PadType PadOverflow = kPadOverflow;
    };

    TLSNodeMemoryPool() {
        ChunckPool = TLSChunkMemoryPool<T>::GetInstance();
        Head = NULL;
        FreeNodeCnt = 0;
        ChunckSize = ChunckPool->GetChunkSize();
    }

    ~TLSNodeMemoryPool() {
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
        if (node->PadUnderflow != kPadUnderflow ||
            node->PadOverflow != kPadOverflow) {
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
    TLSChunkMemoryPool<T>* ChunckPool;

    stNode* Head;

    int FreeNodeCnt;
    int ChunckSize;
};