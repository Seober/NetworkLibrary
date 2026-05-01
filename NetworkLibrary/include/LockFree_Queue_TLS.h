#pragma once
#include <windows.h>

#include "MemoryPool_TLS.h"
//#define LFQ_DEBUG

#define LOCKFREE_QUEUE_VERSION 0.1

#define MAXIMUM_MEMORY_RANGE 0x00007ffffffeffff

template <typename T>
class LockFree_Queue_TLS
{
private:
	struct stNode
	{
		T _Data = NULL;
		stNode* pNext = NULL;
		short RefCnt = 0;
		short DeleteFlag = 0;

		void IncrementRef(void) { InterlockedIncrement16(&RefCnt); }
		short DecrementRef(void) { return InterlockedDecrement16(&RefCnt); }
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

		inline bool CAS_Next(unsigned __int64 Swap, unsigned __int64 Comp)
		{
			PVOID retval = InterlockedCompareExchangePointer((PVOID*)&((stNode*)Bit.Index)->pNext, (PVOID)Swap, (PVOID)Comp);
			return retval == (PVOID)Comp;
		}

		stNode_TAGED(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
		inline void operator =(stNode* pNode) { Bit.Index = (unsigned __int64)pNode; }
		inline stNode* operator->(void) { return (stNode*)Bit.Index; }
		inline bool operator ==(stNode_TAGED Comp) { return Data == Comp.Data; }
	};

public:
	LockFree_Queue_TLS(void);

	void Enqueue(T data);
	bool Dequeue(T& tData);

	void Clear(void);

	int GetUseSize(void) { return _Size; }

	////////////////////////////////////////////
	static int Log_GetTotalMemCnt(void) { return MemoryPool_TLS_Chunck<stNode>::GetInstance()->Log_GetTotalMemCnt(); }

	static int GetStackSize(void) { return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetStackSize(); }
	static int GetPool_TotalSize(void) { return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Total(); }
	static int GetPool_UseSize(void) { return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Use(); }
	static int GetPool_FreeSize(void) { return MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetPoolCnt_Free(); }


private:
	inline WORD GetTagCnt(void) { return InterlockedIncrement16(&_TagCnt); }
	inline stNode* AllocNode(void)
	{
		MemoryPool_TLS_Node<stNode>* pNodePool = (MemoryPool_TLS_Node<stNode>*)TlsGetValue(MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetTLSIndex());
		if (pNodePool == NULL)
		{
			pNodePool = new MemoryPool_TLS_Node<stNode>;
			pNodePool->SetTLS();
		}

		return pNodePool->Alloc();
	}

	inline void FreeNode(stNode* pNode)
	{
		MemoryPool_TLS_Node<stNode>* pNodePool = (MemoryPool_TLS_Node<stNode>*)TlsGetValue(MemoryPool_TLS_Chunck<stNode>::GetInstance()->GetTLSIndex());
		if (pNodePool == NULL)
		{
			pNodePool = new MemoryPool_TLS_Node<stNode>;
			pNodePool->SetTLS();
		}
		pNodePool->Free(pNode);
	}

private:

	stNode_TAGED _Head;
	stNode_TAGED _Tail;

	long _Size;
	short _TagCnt;
};


template <typename T>
LockFree_Queue_TLS<T>::LockFree_Queue_TLS(void)
{
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	if (SystemInfo.lpMaximumApplicationAddress != (void*)MAXIMUM_MEMORY_RANGE)
	{
		wprintf(L"LockFree_Stack Version need Update\nNow Version : %f\n", LOCKFREE_STACK_VERSION);
		return;
	}

	_Size = 0;
	_TagCnt = 0;

	/*_Head = NodePool.Alloc();*/
	_Head = AllocNode();
	_Head->pNext = NULL;
	_Head->IncrementRef();
	_Tail = _Head;
}

template <typename T>
void LockFree_Queue_TLS<T>::Enqueue(T data)
{
	stNode_TAGED OldTail;
	stNode_TAGED pNext;
	stNode_TAGED NewNode = AllocNode();
	NewNode->_Data = data;
	NewNode->pNext = NULL;
	NewNode->IncrementRef();
	NewNode.SetTag(GetTagCnt());

	while (1)
	{
		OldTail = _Tail;
		OldTail->IncrementRef();
		pNext = _Tail->pNext;

		if (OldTail == _Tail)
		{
			if (pNext.Data == NULL)
			{
				if (OldTail.CAS_Next(NewNode.Data, pNext.Data))
				{
					_Tail.CAS(NewNode.Data, OldTail.Data);

					if (OldTail->DecrementRef() == 0)
					{
						if (InterlockedExchange16(&OldTail->DeleteFlag, 0) == 1) FreeNode(OldTail.GetPtr());/*NodePool.Free(OldTail.GetPtr());*/
					}
					break;
				}
			}
			else _Tail.CAS(pNext.Data, OldTail.Data);
		}

		if (OldTail->DecrementRef() == 0)
		{
			if (InterlockedExchange16(&OldTail->DeleteFlag, 0) == 1) FreeNode(OldTail.GetPtr()); /*NodePool.Free(OldTail.GetPtr());*/
		}
	}
	InterlockedExchangeAdd(&_Size, 1);
}


template <typename T>
bool LockFree_Queue_TLS<T>::Dequeue(T& tData)
{
	if (InterlockedExchangeAdd(&_Size, -1) <= 0)
	{
		InterlockedExchangeAdd(&_Size, 1);
		tData = NULL;
		return false;
	}

	stNode_TAGED CurHead;
	stNode_TAGED CurTail;
	stNode_TAGED pNext;
	pNext.SetTag(GetTagCnt());

	while (1)
	{
		CurHead = _Head;
		CurTail = _Tail;

		pNext = CurHead->pNext;

		if (CurHead == _Head)
		{
			if (CurHead.GetPtr() == CurTail.GetPtr()) _Tail.CAS(pNext.Data, CurTail.Data);
			else
			{
				tData = pNext->_Data;
				if (_Head.CAS(pNext.Data, CurHead.Data))
				{
					InterlockedExchange16(&CurHead->DeleteFlag, 1);
					if (CurHead->DecrementRef() == 0)
					{
						if (InterlockedExchange16(&CurHead->DeleteFlag, 0) == 1) FreeNode(CurHead.GetPtr()); /*NodePool.Free(CurHead.GetPtr());*/
					}
					break;
				}
			}
		}
	}

	return true;
}


template <typename T>
void LockFree_Queue_TLS<T>::Clear(void)
{
	stNode_TAGED tmpHead;
	stNode_TAGED pNext;

	while (_Size)
	{
		tmpHead = _Head;
		pNext = tmpHead->pNext;

		if (pNext == NULL) break;

		_Head = pNext;
		tmpHead->RefCnt = 0;
		FreeNode(tmpHead.GetPtr());
		InterlockedExchangeAdd(&_Size, -1);
	}
}