#pragma once
#include <windows.h>
#include <map>

#include "MemoryPool_LF.h"

template <typename T>
class LockFree_Stack {
public:
    static constexpr unsigned __int64 kMaxMemoryRange = 0x00007ffffffeffff;

    struct stNode {
        T _Data = NULL;
        stNode* pNext = NULL;
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

        stNode_TAGED(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
        inline void operator=(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
        inline stNode* operator->(void) { return (stNode*)Bit.Index; }
    };

    LockFree_Stack(void);

    void Push(T tData);
    bool Pop(T& tData);
    T Pop(void);

    inline unsigned int GetUseSize(void) { return _Size; }

    inline int GetTotalPool(void) { return NodePool.GetTotalMemCnt(); }
    inline int GetUsePool(void) { return NodePool.GetUseMemCnt(); }
    inline int GetFreePool(void) { return NodePool.GetFreeMemCnt(); }


private:
    inline short GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }

    stNode_TAGED _Top;

    MemoryPool_LF<stNode> NodePool;


    alignas(64) long _Size;
    alignas(64) short TagCnt;
};


template <typename T>
LockFree_Stack<T>::LockFree_Stack(void) {
    _Size = 0;
    TagCnt = 0;

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    if (SystemInfo.lpMaximumApplicationAddress != (void*)kMaxMemoryRange) {
        wprintf(L"LockFree_Stack: user-space memory range mismatch (47-bit pointer encoding may break)\n");
        return;
    }
}


template <typename T>
void LockFree_Stack<T>::Push(T tData) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop = NodePool.Alloc();
    NewTop.SetTag(GetTagCnt());
    NewTop->_Data = tData;

    do {
        OldTop = _Top;
        NewTop->pNext = OldTop.GetPtr();

    } while (!_Top.CAS(NewTop.Data, OldTop.Data));

    InterlockedExchangeAdd(&_Size, 1);
}


template <typename T>
T LockFree_Stack<T>::Pop(void) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop;
    NewTop.SetTag(GetTagCnt());

    if (InterlockedExchangeAdd(&_Size, -1) <= 0) {
        InterlockedExchangeAdd(&_Size, 1);
        return NULL;
    }
    do {
        OldTop = _Top;
        NewTop = OldTop->pNext;

    } while (!_Top.CAS(NewTop.Data, OldTop.Data));

    T returnData = OldTop->_Data;
    NodePool.Free(OldTop.GetPtr());

    return returnData;
}


template <typename T>
bool LockFree_Stack<T>::Pop(T& tData) {
    stNode_TAGED OldTop;
    stNode_TAGED NewTop;
    NewTop.SetTag(GetTagCnt());

    if (InterlockedExchangeAdd(&_Size, -1) <= 0) {
        InterlockedExchangeAdd(&_Size, 1);
        tData = NULL;
        return false;
    }
    do {
        OldTop = _Top;
        NewTop = OldTop->pNext;

    } while (!_Top.CAS(NewTop.Data, OldTop.Data));

    tData = OldTop->_Data;
    NodePool.Free(OldTop.GetPtr());

    return true;
}