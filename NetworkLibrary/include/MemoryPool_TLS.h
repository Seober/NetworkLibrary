#pragma once

#include "LockFree_Stack.h"
#include "MemoryPool_LF.h"

#define MEMORYPOOL_TLS_VERSION 0.1
#define MEMORYPOOL_TLS_PAD_TYPE unsigned __int64
#define MEMORYPOOL_TLS_PAD_UNDERFLOW 0xdfdfdfdfdfdfdfdf
#define MEMORYPOOL_TLS_PAD_OVERFLOW 0xefefefefefefefef

template <typename T>
class MemoryPool_TLS_Chunck
{
public:
	enum CHUNCK_SIZE
	{
		eCHUNCK_DEFAULT = 100
	};

	void* AllocChunck()
	{
		void* pChunck;
		if (ChunckStack.Pop(pChunck) == false)
		{
			InterlockedExchangeAdd(&TotalSize, eCHUNCK_DEFAULT);
			InterlockedExchangeAdd(&UseSize, eCHUNCK_DEFAULT);
			pChunck = NULL;
		}
		else
		{
			InterlockedExchangeAdd(&FreeSize, -eCHUNCK_DEFAULT);
			InterlockedExchangeAdd(&UseSize, eCHUNCK_DEFAULT);
		}
		return pChunck;
	}

	void FreeChunck(void* pChunck)
	{
		int ChunckSize = eCHUNCK_DEFAULT;
		InterlockedExchangeAdd(&UseSize, -eCHUNCK_DEFAULT);
		InterlockedExchangeAdd(&FreeSize, eCHUNCK_DEFAULT);
		ChunckStack.Push(pChunck);
	}


	static MemoryPool_TLS_Chunck* GetInstance(void)
	{
		if (pChunckPool == NULL)
		{
			Lock();
			if (pChunckPool == NULL)
			{

				pChunckPool = new MemoryPool_TLS_Chunck;
				atexit(Destroy);
			}
			Unlock();
		}

		return pChunckPool;
	}

	DWORD GetTLSIndex(void) { return TLS_idx; }
	int GetChunckSize(void) { return eCHUNCK_DEFAULT; }

	int GetTotalMemCnt(void) { return TotalSize; }
	int GetUseMemCnt(void) { return UseSize; }
	int GetFreeMemCnt(void) { return FreeSize; }


	int GetStackSize(void) { return ChunckStack.GetUseSize(); }
	int GetPoolCnt_Total(void) { return ChunckStack.GetTotalPool(); }
	int GetPoolCnt_Use(void) { return ChunckStack.GetUsePool(); }
	int GetPoolCnt_Free(void) { return ChunckStack.GetFreePool(); }

private:
	MemoryPool_TLS_Chunck()
	{
		TLS_idx = TlsAlloc();

		TotalSize = 0;
		UseSize = 0;
		FreeSize = 0;
	}
	~MemoryPool_TLS_Chunck() { TlsFree(TLS_idx); }

	static void Lock() { while (InterlockedExchange(&Key_Singleton, 1) != 0); }
	static void Unlock() { Key_Singleton = 0; }

	static void Destroy(void)
	{
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
class MemoryPool_TLS_Node
{
public:
	struct stNode
	{
		MEMORYPOOL_TLS_PAD_TYPE Pad_UnderFlow = MEMORYPOOL_TLS_PAD_UNDERFLOW;
		T Data;
		stNode* pNext = NULL;
		MEMORYPOOL_TLS_PAD_TYPE Pad_OverFlow = MEMORYPOOL_TLS_PAD_OVERFLOW;
	};

	MemoryPool_TLS_Node()
	{
		ChunckPool = MemoryPool_TLS_Chunck<T>::GetInstance();
		_Head = NULL;
		_iFreeNodeCnt = 0;
		_iChunckSize = ChunckPool->GetChunckSize();
	}

	~MemoryPool_TLS_Node()
	{
		while (_Head != NULL)
		{
			stNode* pNext = _Head->pNext;
			delete _Head;
			_Head = pNext;
		}
		_iFreeNodeCnt = 0;
	}

	void SetTLS(void)
	{
		TlsSetValue(ChunckPool->GetTLSIndex(), this);
	}

	T* Alloc()
	{
		if (_iFreeNodeCnt == 0)
		{
			_Head = (stNode*)ChunckPool->AllocChunck();
			if (_Head == NULL) _Head = NewNodeList();
			_iFreeNodeCnt += _iChunckSize;
		}

		stNode* pNode = _Head;
		_Head = _Head->pNext;
		_iFreeNodeCnt--;

		return &pNode->Data;
	}

	void Free(T* pTarget)
	{
		stNode* pNode = (stNode*)((char*)pTarget - sizeof(MEMORYPOOL_TLS_PAD_TYPE));
		if (pNode->Pad_UnderFlow != MEMORYPOOL_TLS_PAD_UNDERFLOW || pNode->Pad_OverFlow != MEMORYPOOL_TLS_PAD_OVERFLOW)
		{
			char* Err_Make = NULL;
			*Err_Make = 10;
		}

		pNode->pNext = _Head;
		_Head = pNode;
		if (++_iFreeNodeCnt >= _iChunckSize)
		{
			for (int i = 0; i < _iChunckSize; i++) _Head = _Head->pNext;
			_iFreeNodeCnt -= _iChunckSize;
			ChunckPool->FreeChunck(pNode);
		}
	}

private:
	stNode* NewNodeList()
	{
		stNode* pHead = _Head;

		for (int i = 0; i < _iChunckSize; i++)
		{
			stNode* pNewNode = new stNode;

			pNewNode->pNext = pHead;
			pHead = pNewNode;
		}

		return pHead;
	}

private:
	MemoryPool_TLS_Chunck<T>* ChunckPool;

	stNode* _Head;

	int _iFreeNodeCnt;
	int _iChunckSize;
};