#pragma once

#include <windows.h>

#define MEMORYPOOL_LF_VERSION 0.1
#define MEMORYPOOL_LF_PAD_TYPE unsigned __int64
#define MEMORYPOOL_LF_PAD_UNDERFLOW 0xdfdfdfdfdfdfdfdf
#define MEMORYPOOL_LF_PAD_OVERFLOW 0xefefefefefefefef

#define MAXIMUM_MEMORY_RANGE 0x00007ffffffeffff

template <typename T>
class MemoryPool_LF
{
public:
	enum DEFALUT
	{
		eDEFAULT_POOL = 300
	};

	struct stNode
	{ //abcdef
		MEMORYPOOL_LF_PAD_TYPE Pad_UnderFlow = MEMORYPOOL_LF_PAD_UNDERFLOW;
		T Data;
		stNode* pNext = NULL;
		MEMORYPOOL_LF_PAD_TYPE Pad_OverFlow = MEMORYPOOL_LF_PAD_OVERFLOW;
	};

	struct BitField
	{
		unsigned __int64 Index : 47;
		unsigned __int64 Flag : 1;
		unsigned __int64 Tag : 16;
	};

	union stNode_TAGED
	{
		unsigned __int64 Data;
		BitField Bit;

		stNode_TAGED() { Data = 0; }

		inline void SetTag(WORD Tag) { Bit.Tag = Tag; }

		inline WORD GetTag() { return Bit.Tag; }
		inline stNode* GetPtr() { return (stNode*)Bit.Index; }
		inline bool CAS(unsigned __int64 Swap, unsigned __int64 Comp)
		{
			PVOID retval = InterlockedCompareExchangePointer((PVOID*)&Data, (PVOID)Swap, (PVOID)Comp);
			return retval == (PVOID)Comp;
		}

		stNode_TAGED(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
		inline void operator =(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
		inline stNode* operator->(void) { return (stNode*)Bit.Index; }
	};

	MemoryPool_LF(int initSize = eDEFAULT_POOL);
	~MemoryPool_LF(void) { if (iCnt_TotalNode == iCnt_FreeNode + iCnt_UseNode) Clear_MemoryPool(); }

	inline T* Alloc(void);
	/*inline void Alloc(T* pTarget);*/

	inline void Free(T* pTarget);

	int GetUseMemCnt(void) { return iCnt_UseNode; }
	int GetFreeMemCnt(void) { return iCnt_FreeNode; }
	int GetTotalMemCnt(void) { return iCnt_TotalNode; }

private:
	inline void Clear_MemoryPool(void);
	inline void NewNode(void);
	inline WORD GetTagCnt(void) { return InterlockedIncrement16(&TagCnt); }

	inline void PushtoFreeNode(stNode* pNode);
	inline stNode* PopFromFreeNode(void)
	{
		stNode_TAGED OldTop;
		stNode_TAGED NewTop;
		NewTop.SetTag(GetTagCnt());

		if (InterlockedExchangeAdd(&iCnt_FreeNode, -1) <= 0)
		{
			InterlockedExchangeAdd(&iCnt_FreeNode, 1);
			return NULL;
		}

		do
		{
			OldTop = Head_FreeNode;

			NewTop = OldTop->pNext;

		} while (!Head_FreeNode.CAS(NewTop.Data, OldTop.Data));

		return OldTop.GetPtr();
	}


private:
	stNode_TAGED Head_FreeNode;

	long iCnt_FreeNode;
	long iCnt_UseNode;

	long iCnt_TotalNode;

	long iMemoryAllocSize;

	short TagCnt;
};



template <typename T>
MemoryPool_LF<T>::MemoryPool_LF(int initSize)
{
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	if (SystemInfo.lpMaximumApplicationAddress != (PVOID)MAXIMUM_MEMORY_RANGE)
	{
		wprintf(L"LockFree_Stack Version need Update\nNow Version : %f\n", MEMORYPOOL_LF_VERSION);
		return;
	}

	iMemoryAllocSize = initSize;
	TagCnt = 0;
	iCnt_FreeNode = 0;
	iCnt_UseNode = 0;
	iCnt_TotalNode = 0;

	for (unsigned int i = 0; i < iMemoryAllocSize; i++) NewNode();
}

template <typename T>
void MemoryPool_LF<T>::Clear_MemoryPool(void)
{
	while (1)
	{
		stNode* pDeleteNode = PopFromFreeNode();
		if (pDeleteNode == NULL) break;
		delete pDeleteNode;
	}

	iCnt_FreeNode = 0;
	iCnt_UseNode = 0;
	iCnt_TotalNode = 0;
}


template <typename T>
void MemoryPool_LF<T>::NewNode(void)
{
	stNode* pNode = new stNode;

	PushtoFreeNode(pNode);
	InterlockedIncrement(&iCnt_TotalNode);
}


template <typename T>
T* MemoryPool_LF<T>::Alloc(void)
{
	stNode* pNode = PopFromFreeNode();
	while (!pNode)
	{
		for (int i = 0; i < iMemoryAllocSize; i++) NewNode();
		pNode = PopFromFreeNode();
	}
	InterlockedIncrement(&iCnt_UseNode);

	return &pNode->Data;
}


template <typename T>
void MemoryPool_LF<T>::Free(T* pTarget)
{
	stNode* pNode = (stNode*)((char*)pTarget - sizeof(MEMORYPOOL_LF_PAD_TYPE));
	if (pNode->Pad_UnderFlow != MEMORYPOOL_LF_PAD_UNDERFLOW || pNode->Pad_OverFlow != MEMORYPOOL_LF_PAD_OVERFLOW)
	{
		char* Err_Make = NULL;
		*Err_Make = 10;
	}

	PushtoFreeNode(pNode);
	InterlockedDecrement(&iCnt_UseNode);
}

template <typename T>
void MemoryPool_LF<T>::PushtoFreeNode(stNode* pNode)
{
	stNode_TAGED OldTop;
	stNode_TAGED NewTop = pNode;
	NewTop.SetTag(GetTagCnt());
	do
	{
		OldTop = Head_FreeNode;
		NewTop->pNext = OldTop.GetPtr();

	} while (!Head_FreeNode.CAS(NewTop.Data, OldTop.Data));

	InterlockedIncrement(&iCnt_FreeNode);
}