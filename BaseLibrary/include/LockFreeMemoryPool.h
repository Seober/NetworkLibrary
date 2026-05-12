#pragma once

#include <windows.h>

template <typename T>
class LockFreeMemoryPool {
public:
    static constexpr int kDefaultPool = 300;
    static constexpr unsigned __int64 kMaxMemoryRange = 0x00007ffffffeffff;

    using PadType = unsigned __int64;
    static constexpr PadType kPadUnderflow = 0xdfdfdfdfdfdfdfdf;
    static constexpr PadType kPadOverflow = 0xefefefefefefefef;

    struct stNode {  //abcdef
        PadType PadUnderflow = kPadUnderflow;
        T Data;
        stNode* Next = nullptr;
        PadType PadOverflow = kPadOverflow;
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

        stNode_TAGED(stNode* node) { Bit.Index = (unsigned __int64)node; }
        inline void operator=(stNode* node) { Bit.Index = (unsigned __int64)node; }
        inline stNode* operator->(void) { return (stNode*)Bit.Index; }
    };

    LockFreeMemoryPool(int initSize = kDefaultPool);
    ~LockFreeMemoryPool(void) {
        if (TotalNodeCnt == FreeNodeCnt + UseNodeCnt)
            ClearMemoryPool();
    }

    inline T* Alloc(void);
    /*inline void Alloc(T* target);*/

    inline void Free(T* target);

    int GetUseMemCnt(void) { return UseNodeCnt; }
    int GetFreeMemCnt(void) { return FreeNodeCnt; }
    int GetTotalMemCnt(void) { return TotalNodeCnt; }

private:
    inline void ClearMemoryPool(void);
    inline void NewNode(void);
    inline WORD GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }

    inline void PushtoFreeNode(stNode* node);
    inline stNode* PopFromFreeNode(void) {
        stNode_TAGED OldTop;
        stNode_TAGED NewTop;
        NewTop.SetTag(GetTagCnt());

        if (InterlockedExchangeAdd(&FreeNodeCnt, -1) <= 0) {
            InterlockedExchangeAdd(&FreeNodeCnt, 1);
            return nullptr;
        }

        do {
            OldTop = HeadFreeNode;

            NewTop = OldTop->Next;

        } while (!HeadFreeNode.CAS(NewTop.Data, OldTop.Data));

        return OldTop.GetPtr();
    }


private:
    stNode_TAGED HeadFreeNode;

    long FreeNodeCnt;
    long UseNodeCnt;

    long TotalNodeCnt;

    long MemoryAllocSize;

    short TagCnt;
};


template <typename T>
LockFreeMemoryPool<T>::LockFreeMemoryPool(int initSize) {
    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    if (SystemInfo.lpMaximumApplicationAddress != (PVOID)kMaxMemoryRange) {
        wprintf(L"LockFreeMemoryPool: user-space memory range mismatch (47-bit pointer encoding may break)\n");
        return;
    }

    MemoryAllocSize = initSize;
    TagCnt = 0;
    FreeNodeCnt = 0;
    UseNodeCnt = 0;
    TotalNodeCnt = 0;

    for (int i = 0; i < MemoryAllocSize; i++)
        NewNode();
}

template <typename T>
void LockFreeMemoryPool<T>::ClearMemoryPool(void) {
    while (1) {
        stNode* deleteNode = PopFromFreeNode();
        if (deleteNode == nullptr)
            break;
        delete deleteNode;
    }

    FreeNodeCnt = 0;
    UseNodeCnt = 0;
    TotalNodeCnt = 0;
}


template <typename T>
void LockFreeMemoryPool<T>::NewNode(void) {
    stNode* node = new stNode;

    PushtoFreeNode(node);
    InterlockedIncrement(&TotalNodeCnt);
}


template <typename T>
T* LockFreeMemoryPool<T>::Alloc(void) {
    stNode* node = PopFromFreeNode();
    while (!node) {
        for (int i = 0; i < MemoryAllocSize; i++)
            NewNode();
        node = PopFromFreeNode();
    }
    InterlockedIncrement(&UseNodeCnt);

    return &node->Data;
}


template <typename T>
void LockFreeMemoryPool<T>::Free(T* target) {
    stNode* node = (stNode*)((char*)target - sizeof(PadType));
    if (node->PadUnderflow != kPadUnderflow ||
        node->PadOverflow != kPadOverflow) {
        char* Err_Make = nullptr;
        *Err_Make = 10;
    }

    PushtoFreeNode(node);
    InterlockedDecrement(&UseNodeCnt);
}

template <typename T>
void LockFreeMemoryPool<T>::PushtoFreeNode(stNode* node) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop = node;
    NewTop.SetTag(GetTagCnt());
    do {
        OldTop = HeadFreeNode;
        NewTop->Next = OldTop.GetPtr();

    } while (!HeadFreeNode.CAS(NewTop.Data, OldTop.Data));

    InterlockedIncrement(&FreeNodeCnt);
}