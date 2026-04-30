#include "CNet_Server.h"


#include "MemoryPool_TLS.h"
#include "LockFree_Stack.h"
#include "Logger.h"

////////////////////////////////////////////////////////////////////////////////////////////////

unsigned WINAPI CNet_Server::AcceptThread(LPVOID lpThreadParameter)
{
	Logger* pLogger = Logger::GetInstance();
	MemoryPool_TLS_Node<CPacket> PacketPool;
	PacketPool.SetTLS();

	CNet_Server* pServer = (CNet_Server*)lpThreadParameter;

	DWORD dwThreadID = GetCurrentThreadId();

	SOCKET ClientSocket;
	SOCKADDR_IN ClientAddr;
	int AddrSize = sizeof(ClientAddr);

	while (1)
	{
		ClientSocket = accept(pServer->ListenSocket, (SOCKADDR*)&ClientAddr, &AddrSize);
		if (ClientSocket == INVALID_SOCKET)
		{
			int Err_Code = WSAGetLastError();
			switch (Err_Code)
			{
			case 10004:
				break;
			default:
				pLogger->Log(L"Accept", Logger::en_LOG_LEVEL::eLEVEL_SYSTEM, L"AcceptFuc Err:%d\nMake Crash", Err_Code);
				pLogger->Crash();
				break;
			}
		}// @@@@@ 단순 메모리 정리로 변경필요

		pServer->AddAcceptCnt();

		if (!pServer->OnConnectionRequest())
		{
			closesocket(ClientSocket);
			pLogger->Log(L"Accept", Logger::en_LOG_LEVEL::eLEVEL_DEBUG, L"Max User Connected, ConnectedSession:%d\n", pServer->GetSessionCnt_Connected());
			continue;
		}

		CNet_Server::stSESSION* pSession = pServer->GetFreeSession();
		if (pSession == NULL)
		{
			closesocket(ClientSocket);
			pLogger->Log(L"Accept", Logger::en_LOG_LEVEL::eLEVEL_DEBUG, L"Not Enough FreeSession, ConnectedSession:%d\n", pServer->GetSessionCnt_Connected());
			continue;
		}

		pSession->Socket = ClientSocket;
		/*InterlockedExchange(&pSession->Socket, ClientSocket);*/
		pSession->ClientAddr = ClientAddr;

		CreateIoCompletionPort((HANDLE)pSession->Socket, pServer->h_IOCP, (ULONG_PTR)pSession, 0);

		pServer->OnClientJoin(pSession->SessionID);

		pServer->RecvPost(pSession);

		
		if (pSession->DecrementSessionRef() == 0) pServer->DisconnectSession(pSession);
	}

	return 0;
}

unsigned WINAPI CNet_Server::WorkerThread(LPVOID lpThreadParameter)
{
	Logger* pLogger = Logger::GetInstance();
	MemoryPool_TLS_Node<CPacket> PacketPool;
	PacketPool.SetTLS();

	CNet_Server* pServer = (CNet_Server*)lpThreadParameter;

	DWORD dwThreadID = GetCurrentThreadId();

	DWORD dwTransferred;
	CNet_Server::stSESSION* targetSession;
	OVERLAPPED* tmpOverlapped;

	while (1)
	{
		GetQueuedCompletionStatus(pServer->h_IOCP, &dwTransferred, (PULONG_PTR)&targetSession, &tmpOverlapped, INFINITE);

		if (dwTransferred == 0 && targetSession == NULL && tmpOverlapped == NULL)
		{
			pLogger->Log(L"Network", Logger::en_LOG_LEVEL::eLEVEL_SYSTEM, L"# GQCS return NULL");
			pLogger->Crash();
			break;
		}

		if (dwTransferred != 0)
		{
			// Recv 완료통지	
			if (&targetSession->RecvOverlapped == tmpOverlapped)
			{
				/*pServer->AddRecv(dwTransferred);*/
				pServer->AddRecvBytes(dwTransferred);
				targetSession->RecvQ.MoveWritePos(dwTransferred);


				while (1)
				{
					if (!pServer->CheckPacketMessageComplete(targetSession->SessionID, &targetSession->RecvQ)) 
						break;

					CPacket* pPacket = pServer->AllocPacket();
					if (pServer->GetPacketMessage(pPacket, &targetSession->RecvQ) == false) // Decode Fail, Need to Disconnect
					{
						pServer->FreePacket(pPacket);
						pServer->KillSession(targetSession->SessionID);

						WCHAR szIPBUF[32];
						pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_SYSTEM, L"Packet Decode Failed, SessionID:%p, IP:%s", targetSession->SessionID,
							InetNtop(AF_INET, &targetSession->ClientAddr.sin_addr, szIPBUF, 32));
						break;
					}
					pServer->AddRecvPacket();
					pServer->OnRecv(targetSession->SessionID, pPacket);

					pServer->FreePacket(pPacket);
				}
				int RemainSize = targetSession->RecvQ.GetDataSize();
				if (RemainSize) targetSession->RecvQ.ShiftDataToFront();
				else targetSession->RecvQ.Clear();

				pServer->RecvPost(targetSession);
			}

			// Send 완료통지
			else if (&targetSession->SendOverlapped == tmpOverlapped)
			{
				pServer->AddSend(targetSession->dwSendPacketCnt, dwTransferred);

				int tmpPacketCnt = targetSession->dwSendPacketCnt;
				for (int i = 0; i < tmpPacketCnt; i++)
				{
					CPacket* pPacket = targetSession->SendBuffer[i];
					pServer->FreePacket(pPacket);
				}
				targetSession->dwSendPacketCnt = 0;

				targetSession->dwSendFlag = 0;

				targetSession->IncrementSessionRef();
				if (!pServer->SendPost(targetSession)) targetSession->DecrementSessionRef();
			}
			
			// SendPacket
			else if (tmpOverlapped == (LPOVERLAPPED)0xfffffffffffffffe)
			{
				targetSession->IncrementSessionRef();
				if (!pServer->SendPost(targetSession)) targetSession->DecrementSessionRef();
			}


			else
			{
				pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"Overlapped Pointer Error, SessionID:%p, UseFlag:%d, DeleteFlag:%d, SessionRef:%d",
					targetSession->SessionID, targetSession->SessionUseFlag, targetSession->ReleaseArr[0], targetSession->ReleaseArr[1]);
				pLogger->Crash();
			}
		}
		
		if (targetSession->DecrementSessionRef() == 0) pServer->DisconnectSession(targetSession);
	}
	
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

CNet_Server::CNet_Server()
{
	h_IOCP = NULL;
	ListenSocket = INVALID_SOCKET;

	SessionArr = NULL;
	SessionCnt_Total = 0;

	SessionID_Cnt = 0;

	dwAcceptThreadID = 0;
	hAcceptThread = NULL;

	dwWorkerThreadID = NULL;
	hWorkerThread = NULL;
	ThreadCnt = 0;

	_AcceptTotal = 0;
	_AcceptTPS = 0;

	pPacketPool = MemoryPool_TLS_Chunck<CPacket>::GetInstance();

	InitializeSRWLock(&srwLogTransmitMap);
}

CNet_Server::~CNet_Server()
{
	for (int i = 0; i < ThreadCnt; i++) CloseHandle(hWorkerThread[i]);
	delete[] hWorkerThread;
	delete[] dwWorkerThreadID;

	if (SessionCnt_Total) delete[] SessionArr;

	CloseHandle(hAcceptThread);

	closesocket(ListenSocket);

	WSACleanup();
}

bool CNet_Server::Start(const WCHAR* ServerIP, u_short ServerPort, u_short WorkerThreadCnt_Total, u_short WorkerThreadCnt_Run, BOOL Nagle, u_short ConnectSession_Max)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

	h_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WorkerThreadCnt_Run);
	if (h_IOCP == NULL) return false;

	ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (ListenSocket == INVALID_SOCKET) return false;

	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	int retval_setsockopt = setsockopt(ListenSocket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	if (retval_setsockopt == SOCKET_ERROR) return false;

	/*int sndbuf = 0;
	retval_setsockopt = setsockopt(ListenSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));
	if (retval_setsockopt == SOCKET_ERROR) return false;*/

	if (Nagle)
	{
		BOOL NagleValue = TRUE;
		retval_setsockopt = setsockopt(ListenSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&NagleValue, sizeof(NagleValue));
		if (retval_setsockopt == SOCKET_ERROR) return false;
	}

	SOCKADDR_IN serveraddr;
	serveraddr.sin_family = AF_INET;
	if (ServerIP == NULL) serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	else InetPton(AF_INET, ServerIP, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(ServerPort);

	int retval_bind = bind(ListenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval_bind == SOCKET_ERROR) return false;

	int retval_listen = listen(ListenSocket, SOMAXCONN_HINT(60000));
	if (retval_listen == SOCKET_ERROR) return false;

	wprintf(L"[%s:%d] Listening\n", ServerIP, ServerPort);

	//////////////////////////////////////////

	SessionCnt_Total = ConnectSession_Max;
	SessionArr = new stSESSION[SessionCnt_Total];
	for (u_short init_SessionArr = 0; init_SessionArr < SessionCnt_Total; init_SessionArr++)
	{
		SessionArr[init_SessionArr].ReleaseArr[0] = TRUE;
		SessionArr[init_SessionArr].ReleaseArr[1] = 0;
		SessionArr[init_SessionArr].SessionID = (unsigned __int64)init_SessionArr << 48;

		FreeSessionStack.Push(&SessionArr[init_SessionArr]);
	}

	hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, (LPVOID)this, 0, (unsigned int*)&dwAcceptThreadID);
	if (hAcceptThread == NULL) return false;

	ThreadCnt = WorkerThreadCnt_Total;
	dwWorkerThreadID = new DWORD[ThreadCnt];
	hWorkerThread = new HANDLE[ThreadCnt];

	for (int i = 0; i < ThreadCnt; i++)
	{
		hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0, (unsigned int*)&dwWorkerThreadID[i]);
		if (hWorkerThread[i] == NULL) return false;
	}

	return true;
}

void CNet_Server::SendPacket(unsigned __int64 SessionID, CPacket* pPacket)
{
	stSESSION* pSession = FindSession(SessionID);

	pSession->IncrementSessionRef();
	if (pSession->ReleaseArr[0] == FALSE && pSession->SessionID == SessionID)
	{
		pPacket->IncrementRef();
		
		EncodePacket(pPacket);
		pSession->SendQ.Enqueue(pPacket);

		
		PostQueuedCompletionStatus(h_IOCP, 1, (ULONG_PTR)pSession, (LPOVERLAPPED)0xfffffffffffffffe);
	}
	else pSession->DecrementSessionRef();
}

bool CNet_Server::SendPost(stSESSION* pSession, DWORD Flag)
{
	if (pSession->SessionUseFlag == false) return false;
	if (InterlockedExchange(&pSession->dwSendFlag, 1) == 1) return false;

	int PacketCnt = pSession->SendQ.GetUseSize();
	while (!PacketCnt)
	{
		pSession->dwSendFlag = 0;

		if (pSession->SendQ.GetUseSize())
		{
			if (InterlockedExchange(&pSession->dwSendFlag, 1) == 0)
			{
				PacketCnt = pSession->SendQ.GetUseSize();
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
		if (pSession->SendQ.Dequeue(tmpPacket) == false)
		{
			Logger* pLogger = Logger::GetInstance();
			pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"SendQ->Dequeue Failed, SessionID:%p, tmpRemainCnt:%d, SendQRemainCnt:%d,",
				pSession->SessionID, PacketCnt, pSession->SendQ.GetUseSize());
			pLogger->Crash();
		}

		wsabuf[wsabufCnt].buf = tmpPacket->GetReadBufferPtr();
		wsabuf[wsabufCnt].len = tmpPacket->GetDataSize();
		pSession->SendBuffer[wsabufCnt] = tmpPacket;
		wsabufCnt++;
	}

	pSession->dwSendPacketCnt = wsabufCnt;

	int retval_WSASend = WSASend(pSession->Socket, wsabuf, wsabufCnt, NULL, Flag, &pSession->SendOverlapped, NULL);
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
		{
			Logger* pLogger = Logger::GetInstance();
			pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# WSASend Func Err # ErrCode:%d, SessionID:%p, UseFlag:%d, DeleteFlag:%d, SessionRef:%d",
				Err_Code, pSession->SessionID, pSession->SessionUseFlag, pSession->ReleaseArr[0], pSession->ReleaseArr[1]);
			pLogger->Crash();
		}
			return false;
		}
	}

	if (pSession->SessionUseFlag == false) CancelIoEx((HANDLE)pSession->Socket, NULL);
	return true;
}


bool CNet_Server::RecvPost(stSESSION* pSession, DWORD Flag)
{
	if (pSession->SessionUseFlag == false) return false;

	WSABUF wsabuf;
	wsabuf.buf = pSession->RecvQ.GetWriteBufferPtr();
	wsabuf.len = pSession->RecvQ.GetFreeSize();

	pSession->IncrementSessionRef();
	int retval_WSARecv = WSARecv(pSession->Socket, &wsabuf, 1, NULL, &Flag, &pSession->RecvOverlapped, NULL);
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
			pSession->DecrementSessionRef();
			return false;
			break;
		default:
		{
			Logger* pLogger = Logger::GetInstance();
			pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# WSARecv Func Err # ErrCode:%d, SessionID:%p, UseFlag:%d, DeleteFlag:%d, SessionRef:%d",
				Err_Code, pSession->SessionID, pSession->SessionUseFlag, pSession->ReleaseArr[0], pSession->ReleaseArr[1]);
			pLogger->Crash();
		}
			break;
		}
	}
	if (pSession->SessionUseFlag == false) CancelIoEx((HANDLE)pSession->Socket, NULL);
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////


CNet_Server::stSESSION* CNet_Server::GetFreeSession()
{
	stSESSION* pSession;
	FreeSessionStack.Pop(pSession);
	if (pSession == NULL) return NULL;

	initSession(pSession);

	return pSession;
}

void CNet_Server::ReleaseSession(stSESSION* pSession)
{
	pSession->SessionID &= 0xffff000000000000;
	closesocket(pSession->Socket);
	/*InterlockedExchange(&pSession->Socket, INVALID_SOCKET);*/

	int RemainSize = pSession->SendQ.GetUseSize();
	CPacket* pPacket;
	while (pSession->SendQ.Dequeue(pPacket)) FreePacket(pPacket);

	RemainSize = pSession->dwSendPacketCnt;
	for (int i = 0; i < RemainSize; i++) FreePacket(pSession->SendBuffer[i]);
	pSession->dwSendPacketCnt = 0;

	FreeSessionStack.Push(pSession);
}

void CNet_Server::initSession(stSESSION* pSession)
{
	pSession->SessionUseFlag = true;
	pSession->SessionID |= GetSessionID_New();

	pSession->LastAction = NULL;

	ZeroMemory(&pSession->SendOverlapped, sizeof(pSession->SendOverlapped));
	ZeroMemory(&pSession->RecvOverlapped, sizeof(pSession->RecvOverlapped));

	pSession->dwSendFlag = 0;
	pSession->dwSendPacketCnt = 0;

	pSession->RecvQ.Clear();
	pSession->SendQ.Clear();

	pSession->IncrementSessionRef();
	pSession->ReleaseArr[0] = FALSE;
}

void CNet_Server::KillSession(unsigned __int64 SessionID)
{
	stSESSION* pSession = FindSession(SessionID);
	pSession->IncrementSessionRef();
	if (pSession->ReleaseArr[0] == FALSE && pSession->SessionID == SessionID)
	{
		pSession->SessionUseFlag = false;
		CancelIoEx((HANDLE)pSession->Socket, NULL);
		if (pSession->DecrementSessionRef() == 0) DisconnectSession(pSession);
	}
	else pSession->DecrementSessionRef();
}

void CNet_Server::DisconnectSession(stSESSION* pSession)
{
	LONG64 CompareArr[2] = { FALSE, 0 };
	if (InterlockedCompareExchange128(pSession->ReleaseArr, 0, TRUE, CompareArr))
	{
		OnClientLeave(pSession->SessionID);
		ReleaseSession(pSession);
	}
}


CPacket* CNet_Server::AllocPacket(void)
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

void CNet_Server::FreePacket(CPacket* pPacket)
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


bool CNet_Server::CheckPacketMessageComplete(unsigned __int64 SessionID, CPacket* pRecvQ)
{
	DWORD PacketUseSize = pRecvQ->GetDataSize();
	if (PacketUseSize <= sizeof(stHeader_NET)) return false; // Header Check

	stHeader_NET* pHeader = (stHeader_NET*)pRecvQ->GetReadBufferPtr();
	if (pHeader->Code != eHEADER_CODE || pHeader->Len > 4096) /*ERR_MAKE;*/
	{
		KillSession(SessionID);
		return false;
	}
	if (PacketUseSize < sizeof(stHeader_NET) + pHeader->Len) return false; // Payload Check

	return true;
}


bool CNet_Server::GetPacketMessage(CPacket* pPacket, CPacket* pRecvQ)
{
	if (!DecodePacket(pRecvQ)) return false;

	stHeader_NET* pHeader = (stHeader_NET*)pRecvQ->GetReadBufferPtr();
	pRecvQ->MoveReadPos(sizeof(stHeader_NET));

	int retval_GetData = pRecvQ->GetData(pPacket->GetWriteBufferPtr(), pHeader->Len);
	if (retval_GetData != pHeader->Len)
	{
		Logger* pLogger = Logger::GetInstance();
		pLogger->Log(L"NetServer", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# CPacket GetData Func Err # EnqueueSize:%d, Return:%d", pHeader->Len, retval_GetData);
		pLogger->Crash();
	}
	pPacket->MoveWritePos(pHeader->Len);

	return true;
}

void CNet_Server::SetPacketHeader(CPacket* pPacket)
{
	char* pFront = pPacket->GetReadBufferPtr();
	WORD PacketDataSize = pPacket->GetDataSize();
	BYTE Checksum = 0;
	WORD HeaderSize = sizeof(stHeader_NET);
	pPacket->MoveReadPos(-HeaderSize);

	stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
	pNetHeader->Code = eHEADER_CODE;
	pNetHeader->Len = PacketDataSize;
	pNetHeader->RKey = rand() % 256;

	for (int i = 0; i < PacketDataSize; i++, pFront++) Checksum += *pFront;
	pNetHeader->Checksum = Checksum;
}

void CNet_Server::EncodePacket(CPacket* pPacket)
{
	if (pPacket->CheckFlag_Encode()) return;
	pPacket->LockPacket();
	if (pPacket->CheckFlag_Encode() == false)
	{
		SetPacketHeader(pPacket);
		stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
		BYTE RK = pNetHeader->RKey;
		BYTE P;
		char* pFront = (char*)pNetHeader + sizeof(stHeader_NET) - sizeof(BYTE);

		P = *pFront ^ (RK + 1);
		*pFront = P ^ (eHEADER_KEY + 1);
		pFront++;

		for (int i = 2; i < pNetHeader->Len + 2; i++, pFront++)
		{
			P = *pFront ^ (P + RK + i);
			*pFront = P ^ (*(pFront - 1) + eHEADER_KEY + i);
		}
	}

	pPacket->UnlockPacket();
}


bool CNet_Server::DecodePacket(CPacket* pPacket)
{
	stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
	BYTE RK = pNetHeader->RKey;
	BYTE Checksum = 0;
	unsigned char* pRear = (unsigned char*)pNetHeader + sizeof(stHeader_NET) - sizeof(BYTE) + pNetHeader->Len;
	unsigned char* ptmp = pRear;

	for (int i = pNetHeader->Len + 1; i > 1; i--, ptmp--) *ptmp = *ptmp ^ (*(ptmp - 1) + eHEADER_KEY + i);
	*ptmp = *ptmp ^ (eHEADER_KEY + 1);

	ptmp = pRear;
	for (int i = pNetHeader->Len + 1; i > 1; i--, ptmp--)
	{
		*ptmp = *ptmp ^ (*(ptmp - 1) + RK + i);
		Checksum += *ptmp;
	}
	*ptmp = *ptmp ^ (RK + 1);

	if (pNetHeader->Checksum != Checksum)
	{
		return false;
	}

	return true;
}

DWORD* CNet_Server::GetThreadTransmitArr(void)
{
	// static thread_local: 스레드당 1개 캐시, 호출마다 lock 안 걸림
	// 다중 CNet_Server 인스턴스 사용 시 한계 있음 (1 서버 가정)
	static thread_local DWORD* TransmitArr = nullptr;
	if (TransmitArr != nullptr) return TransmitArr;

	TransmitArr = new DWORD[4]();  // value-init: 0으로 초기화
	DWORD ThreadID = GetCurrentThreadId();

	AcquireSRWLockExclusive(&srwLogTransmitMap);
	_LogTransmit_Map.insert(std::make_pair(ThreadID, TransmitArr));
	ReleaseSRWLockExclusive(&srwLogTransmitMap);

	return TransmitArr;
}

void CNet_Server::AddRecvBytes(DWORD dwRecvBytes)
{
	DWORD* arr = GetThreadTransmitArr();
	dwRecvBytes += 40 * (dwRecvBytes / 1460 + 1);
	InterlockedExchangeAdd(&arr[1], dwRecvBytes);
}

void CNet_Server::AddRecvPacket(void)
{
	DWORD* arr = GetThreadTransmitArr();
	InterlockedIncrement(&arr[0]);
}


//void CNet_Server::AddRecv(DWORD dwRecvBytes)
//{
//	DWORD ThreadID = GetCurrentThreadId();
//	auto iter_LogTransmit = _LogTransmit_Map.find(ThreadID);
//	if (iter_LogTransmit == _LogTransmit_Map.end())
//	{
//		DWORD* TransmitArr = new DWORD[4];
//		TransmitArr[0] = 0; TransmitArr[1] = 0; TransmitArr[2] = 0; TransmitArr[3] = 0;
//
//		AcquireSRWLockExclusive(&srwLogTransmitMap);
//		_LogTransmit_Map.insert(std::make_pair(ThreadID, TransmitArr));
//		ReleaseSRWLockExclusive(&srwLogTransmitMap);
//
//		iter_LogTransmit = _LogTransmit_Map.find(ThreadID);
//	}
//
//	DWORD NetworkHeader = 40;
//	dwRecvBytes += 40 * (dwRecvBytes / 1460 + 1);
//
//	InterlockedIncrement(&iter_LogTransmit->second[0]);
//	InterlockedExchangeAdd(&iter_LogTransmit->second[1], dwRecvBytes);
//}

void CNet_Server::AddSend(DWORD dwSendPacketCnt, DWORD dwSendBytes)
{
	DWORD* arr = GetThreadTransmitArr();
	dwSendBytes += 40 * (dwSendBytes / 1460 + 1);
	InterlockedExchangeAdd(&arr[2], dwSendPacketCnt);
	InterlockedExchangeAdd(&arr[3], dwSendBytes);
}


void CNet_Server::GetTransmit(DWORD* TransmitBuffer)
{
	for (int i = 0; i < 4; i++) TransmitBuffer[i] = 0;

	// 다른 스레드의 첫 호출(insert) 중 iteration race 방지
	AcquireSRWLockShared(&srwLogTransmitMap);
	for (auto& v : _LogTransmit_Map)
	{
		for (int i = 0; i < 4; i++) TransmitBuffer[i] += InterlockedExchange(&v.second[i], 0);
	}
	ReleaseSRWLockShared(&srwLogTransmitMap);
}