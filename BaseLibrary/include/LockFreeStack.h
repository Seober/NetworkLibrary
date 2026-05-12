#pragma once
#include <windows.h>
#include <map>

#include "LockFreeMemoryPool.h"

template <typename T>
class LockFreeStack {
public:
    static constexpr unsigned __int64 kMaxMemoryRange = 0x00007ffffffeffff;

    struct stNode {
        T Data = nullptr;
        stNode* Next = nullptr;
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

    LockFreeStack(void);

    void Push(T tData);
    bool Pop(T& tData);
    T Pop(void);

    inline unsigned int GetUseSize(void) { return Size; }


private:
    inline short GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }

    stNode_TAGED Top;

    LockFreeMemoryPool<stNode> NodePool;


    alignas(64) long Size;
    alignas(64) short TagCnt;
};


template <typename T>
LockFreeStack<T>::LockFreeStack(void) {
    Size = 0;
    TagCnt = 0;

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    if (SystemInfo.lpMaximumApplicationAddress != (void*)kMaxMemoryRange) {
        wprintf(L"LockFreeStack: user-space memory range mismatch (47-bit pointer encoding may break)\n");
        return;
    }
}


template <typename T>
void LockFreeStack<T>::Push(T tData) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop = NodePool.Alloc();
    NewTop.SetTag(GetTagCnt());
    NewTop->Data = tData;

    do {
        OldTop = Top;
        NewTop->Next = OldTop.GetPtr();

    } while (!Top.CAS(NewTop.Data, OldTop.Data));

    InterlockedExchangeAdd(&Size, 1);
}


template <typename T>
T LockFreeStack<T>::Pop(void) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop;
    NewTop.SetTag(GetTagCnt());

    if (InterlockedExchangeAdd(&Size, -1) <= 0) {
        InterlockedExchangeAdd(&Size, 1);
        return nullptr;
    }
    do {
        OldTop = Top;
        NewTop = OldTop->Next;

    } while (!Top.CAS(NewTop.Data, OldTop.Data));

    T returnData = OldTop->Data;
    NodePool.Free(OldTop.GetPtr());

    return returnData;
}


template <typename T>
bool LockFreeStack<T>::Pop(T& tData) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop;
    NewTop.SetTag(GetTagCnt());

    if (InterlockedExchangeAdd(&Size, -1) <= 0) {
        InterlockedExchangeAdd(&Size, 1);
        tData = nullptr;
        return false;
    }
    do {
        OldTop = Top;
        NewTop = OldTop->Next;

    } while (!Top.CAS(NewTop.Data, OldTop.Data));

    tData = OldTop->Data;
    NodePool.Free(OldTop.GetPtr());

    return true;
}