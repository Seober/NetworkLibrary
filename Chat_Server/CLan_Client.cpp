#include "CLan_Client.h"


#include "MemoryPool_TLS.h"
#include "LockFree_Stack.h"

////////////////////////////////////////////////////////////////////////////////////////////////

unsigned WINAPI CLan_Client::WorkerThread(LPVOID lpThreadParameter)
{
	Logger* pLogger = Logger::GetInstance();
	MemoryPool_TLS_Node<CPacket> PacketPool;
	PacketPool.SetTLS();

	CLan_Client* pServer = (CLan_Client*)lpThreadParameter;

	DWORD dwThreadID = GetCurrentThreadId();

	DWORD dwTransferred;
	stClient* pClient;
	OVERLAPPED* tmpOverlapped;

	DWORD Err_Code = NULL;
	while (1)
	{
		int retval_GQCS = GetQueuedCompletionStatus(pServer->h_IOCP, &dwTransferred, (PULONG_PTR)&pClient, &tmpOverlapped, INFINITE);
		if (retval_GQCS == 0)
		{
			Err_Code = GetLastError();
		}

		if (dwTransferred == 0 && pClient == NULL && tmpOverlapped == NULL)
		{
			pLogger->Crash();
			// Post에 의한 스레드 종료 절차
			continue;
		}

		if (dwTransferred != 0)
		{
			// Recv 완료통지	
			if (&pClient->RecvOverlapped == tmpOverlapped)
			{
				pClient->RecvQ.MoveWritePos(dwTransferred);


				while (1)
				{
					if (!pServer->CheckPacketMessageComplete()) break;

					CPacket* pPacket = pServer->AllocPacket();

					pServer->OnRecv(pPacket);

					pServer->FreePacket(pPacket);
				}
				int RemainSize = pClient->RecvQ.GetDataSize();
				if (RemainSize) pClient->RecvQ.ShiftDataToFront();
				else pClient->RecvQ.Clear();

				pServer->RecvPost();
			}

			// Send 완료통지
			else if (&pClient->SendOverlapped == tmpOverlapped)
			{
				int tmpPacketCnt = pClient->dwSendPacketCnt;
				for (int i = 0; i < tmpPacketCnt; i++)
				{
					CPacket* pPacket = pClient->SendBuffer[i];
					pServer->FreePacket(pPacket);
				}

				pClient->dwSendPacketCnt = 0;

				pClient->dwSendFlag = 0;

				pClient->IncrementClientRef();
				if (!pServer->SendPost()) pClient->DecrementClientRef();
			}
			else pLogger->Crash();
		}

		if (pClient->DecrementClientRef() == 0) pServer->Disconnect();
	}

	return 0;
}

void CLan_Client::InitClient(void)
{
	Client.ClientUseFlag = true;

	ZeroMemory(&Client.RecvOverlapped, sizeof(Client.RecvOverlapped));
	ZeroMemory(&Client.SendOverlapped, sizeof(Client.SendOverlapped));

	Client.SendQ.Clear();
	Client.RecvQ.Clear();

	Client.ReleaseArr[0] = FALSE;
}

bool CLan_Client::Connect(const WCHAR* ServerIP, u_short ServerPort, u_long WorkerThreadCnt_Total, u_long WorkerThreadCnt_Run, bool Nagle)
{
	if (Client.ClientUseFlag) pLogger->Crash();
	
	InitClient();

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

	Client.Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (Client.Socket == INVALID_SOCKET) return false;

	h_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WorkerThreadCnt_Run);
	if (h_IOCP == NULL) return false;

	CreateIoCompletionPort((HANDLE)Client.Socket, h_IOCP, (ULONG_PTR)&Client, 0);

	SOCKADDR_IN ServerAddr;
	ServerAddr.sin_family = AF_INET;
	InetPton(AF_INET, ServerIP, &ServerAddr.sin_addr);
	ServerAddr.sin_port = htons(ServerPort);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	hWorkerThread = new HANDLE[WorkerThreadCnt_Total];
	for (u_long i = 0; i < WorkerThreadCnt_Total; i++)
	{
		hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);
		if (hWorkerThread[i] == NULL) return false;
	}
	ThreadCnt = WorkerThreadCnt_Total;

	int retval_connect = connect(Client.Socket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr));
	if (retval_connect == SOCKET_ERROR) return false;

	OnEnterJoinServer();

	RecvPost();
}

bool CLan_Client::Disconnect(void)
{
	LONG64 CompareArr[2] = { FALSE, 0 };

	Client.ClientUseFlag = false;
	CancelIoEx((HANDLE)Client.Socket, NULL);

	if (InterlockedCompareExchange128(Client.ReleaseArr, 0, TRUE, CompareArr))
	{
		OnLeaveServer();
		
		closesocket(Client.Socket);

		int RemainSize = Client.SendQ.GetUseSize();
		CPacket* pPacket;
		while (Client.SendQ.Dequeue(pPacket)) FreePacket(pPacket);

		RemainSize = Client.dwSendPacketCnt;
		for (int i = 0; i < RemainSize; i++) FreePacket(Client.SendBuffer[i]);
		Client.dwSendPacketCnt = 0;

		return true;
	}

	return false;
}

bool CLan_Client::SendPacket(CPacket* pPacket)
{
	Client.IncrementClientRef();
	if (Client.ReleaseArr[0] == FALSE)
	{
		pPacket->IncrementRef();

		SetPacketHeader(pPacket);
		Client.SendQ.Enqueue(pPacket);


		if (!SendPost() && Client.DecrementClientRef() == 0)
		{
			Disconnect();
			return false;
		}
	}
	else
	{
		Client.DecrementClientRef();
		return false;
	}

	return true;
}


bool CLan_Client::SendPost(DWORD Flag)
{
	if (Client.ClientUseFlag == false) return false;
	if (InterlockedExchange(&Client.dwSendFlag, 1) == 1) return false;

	int PacketCnt = Client.SendQ.GetUseSize();
	while (!PacketCnt)
	{
		Client.dwSendFlag = 0;

		if (Client.SendQ.GetUseSize())
		{
			if (InterlockedExchange(&Client.dwSendFlag, 1) == 0)
			{
				PacketCnt = Client.SendQ.GetUseSize();
			}
			else return false;
		}
		else return false;
	}

	int tmpPacketCnt = PacketCnt;

	WSABUF wsabuf[1000];
	int wsabufCnt = 0;
	CPacket* tmpPacket;
	while (PacketCnt--)
	{
		if (Client.SendQ.Dequeue(tmpPacket) == false) pLogger->Crash();

		wsabuf[wsabufCnt].buf = tmpPacket->GetReadBufferPtr();
		wsabuf[wsabufCnt].len = tmpPacket->GetDataSize();
		Client.SendBuffer[wsabufCnt] = tmpPacket;
		wsabufCnt++;
	}

	Client.dwSendPacketCnt = wsabufCnt;

	int retval_WSASend = WSASend(Client.Socket, wsabuf, wsabufCnt, NULL, Flag, &Client.SendOverlapped, NULL);
	if (retval_WSASend == SOCKET_ERROR)
	{
		int Err_Code = WSAGetLastError();
		switch (Err_Code)
		{
		case WSA_IO_PENDING:
			break;
		case 10004:
		case 10022:
		case 10053:
		case 10054: // 피어별 연결 재설정
			return false;
		default:
			pLogger->Crash();
			return false;
		}
	}
	if (Client.ClientUseFlag == false) CancelIoEx((HANDLE)Client.Socket, NULL);
	return true;
}


bool CLan_Client::RecvPost(DWORD Flag)
{
	if (Client.ClientUseFlag == false) return false;

	WSABUF wsabuf;
	wsabuf.buf = Client.RecvQ.GetWriteBufferPtr();
	wsabuf.len = Client.RecvQ.GetFreeSize();
	if (wsabuf.len == 0) pLogger->Crash();

	Client.IncrementClientRef();
	int retval_WSARecv = WSARecv(Client.Socket, &wsabuf, 1, NULL, &Flag, &Client.RecvOverlapped, NULL);
	if (retval_WSARecv == SOCKET_ERROR)
	{
		int Err_Code = WSAGetLastError();
		switch (Err_Code)
		{
		case WSA_IO_PENDING:
			break;
		case 10004:
		case 10022:
		case 10053:
		case 10054: // 피어별 연결 재설정
			Client.DecrementClientRef();
			return false;
		default:
			pLogger->Crash();
			break;
		}
	}
	if (Client.ClientUseFlag == false) CancelIoEx((HANDLE)Client.Socket, NULL);
	return true;
}


void CLan_Client::SetPacketHeader(CPacket* pPacket)
{
	char* pFront = pPacket->GetReadBufferPtr();
	WORD PacketDataSize = pPacket->GetDataSize();
	pPacket->MoveReadPos(-2);

	WORD* pLanHeader = (WORD*)pPacket->GetReadBufferPtr();
	*pLanHeader = PacketDataSize;
}

bool CLan_Client::CheckPacketMessageComplete()
{
	WORD PacketDataSize = Client.RecvQ.GetDataSize();
	WORD* pPacketHeader = (WORD*)Client.RecvQ.GetReadBufferPtr();

	if (*pPacketHeader <= PacketDataSize) return true;
	else return false;
}

void CLan_Client::GetPacketMessage(CPacket* pPacket)
{
	WORD PacketHeader;
	Client.RecvQ >> PacketHeader;

	int retval_GetData = Client.RecvQ.GetData(pPacket->GetWriteBufferPtr(), PacketHeader);
	if (PacketHeader != retval_GetData) pLogger->Crash();
	pPacket->MoveWritePos(PacketHeader);
}



CPacket* CLan_Client::AllocPacket(void)
{
	MemoryPool_TLS_Node<CPacket>* pPacketPool = (MemoryPool_TLS_Node<CPacket>*)TlsGetValue(MemoryPool_TLS_Chunck<CPacket>::GetInstance()->GetTLSIndex());
	if (pPacketPool == NULL)
	{
		pPacketPool = new MemoryPool_TLS_Node<CPacket>;
		pPacketPool->SetTLS();
	}
	CPacket* pPacket = pPacketPool->Alloc();
	pPacket->Clear();
	pPacket->IncrementRef();
	return pPacket;
}

void CLan_Client::FreePacket(CPacket* pPacket)
{
	if (pPacket->DecrementRef() != 0) return;
	MemoryPool_TLS_Node<CPacket>* pPacketPool = (MemoryPool_TLS_Node<CPacket>*)TlsGetValue(MemoryPool_TLS_Chunck<CPacket>::GetInstance()->GetTLSIndex());
	if (pPacketPool == NULL)
	{
		pPacketPool = new MemoryPool_TLS_Node<CPacket>;
		pPacketPool->SetTLS();
	}
	pPacketPool->Free(pPacket);
}


CLan_Client::CLan_Client(void)
{
	pLogger = Logger::GetInstance();

	Client.ClientUseFlag = false;
	Client.ReleaseArr[0] = FALSE;
	Client.ReleaseArr[1] = 0;
	Client.Socket = INVALID_SOCKET;

	ZeroMemory(&Client.RecvOverlapped, sizeof(Client.RecvOverlapped));
	ZeroMemory(&Client.SendOverlapped, sizeof(Client.SendOverlapped));

	Client.dwSendFlag = 0;
	Client.dwSendPacketCnt = 0;

	Client.SendQ.Clear();
	Client.RecvQ.Clear();
}