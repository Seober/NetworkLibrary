//#include "CNet_Server.h"
#include "Content.h"
#include "Protocol.h"

#include "LockFree_Queue_TLS.h"

#define dfMESSAGEBUFSIZE 1000
WCHAR MessageBuf[dfMESSAGEBUFSIZE];

Chat_Server::Chat_Server(int MaxUser, bool HeartBeatFlag)
{
	hContentThread = INVALID_HANDLE_VALUE;
	hTimerThread5000 = INVALID_HANDLE_VALUE;
	hJobEvent = CreateEvent(NULL, false, false, NULL);

	_MaxUser = MaxUser;

	_Running_CurTime = 0;

	_JobTPS = 0;
	UpdateThreadRunningTPS = 0;
	UpdateThreadSleepTime = 0;

	pLogger = Logger::GetInstance();

	hContentThread = (HANDLE)_beginthreadex(NULL, 0, UpdateThread_Chat_Field1, (LPVOID)this, 0, NULL);
	if (HeartBeatFlag) hTimerThread5000 = (HANDLE)_beginthreadex(NULL, 0, TimerThread_Chat_5000, (LPVOID)this, 0, NULL);
}


bool Chat_Server::OnConnectionRequest()
{
	if (GetCharacterSize() < _MaxUser) return true;
	else return false;
}

void Chat_Server::OnClientJoin(unsigned __int64 SessionID)
{
	stJob* pJob = AllocJob(dfJOBTYPE_JOIN, SessionID, NULL);
	JobQueue.Enqueue(pJob);
	SetEvent(hJobEvent);
}
void Chat_Server::OnClientLeave(unsigned __int64 SessionID)
{
	stJob* pJob = AllocJob(dfJOBTYPE_LEAVE, SessionID, NULL);
	JobQueue.Enqueue(pJob);
	SetEvent(hJobEvent);
}


void Chat_Server::OnRecv(unsigned __int64 SessionID, CPacket* pPacket)
{
	pPacket->IncrementRef();
	stJob* pJob = AllocJob(dfJOBTYPE_MESSAGE, SessionID, pPacket);
	JobQueue.Enqueue(pJob);
	SetEvent(hJobEvent);
}

unsigned WINAPI Chat_Server::TimerThread_Chat_5000(LPVOID lpThreadParameter)
{
	Chat_Server* pServer = (Chat_Server*)lpThreadParameter;
	stJob* pJob;

	while (1)
	{
		pJob = pServer->AllocJob(dfJOBTYPE_HEARTBEAT, NULL, NULL);
		pServer->JobQueue.Enqueue(pJob);
		SetEvent(pServer->hJobEvent);

		Sleep(5000);
	}

	return 0;
}


unsigned WINAPI Chat_Server::UpdateThread_Chat_Field1(LPVOID lpThreadParameter)
{
	Chat_Server* pServer = (Chat_Server*)lpThreadParameter;
	stJob* pJob;

	while (1)
	{
		if (pServer->JobQueue.Dequeue(pJob))
		{
			InterlockedIncrement(&pServer->_JobTPS);

			switch (pJob->JobType)
			{
			case dfJOBTYPE_MESSAGE:
				pServer->MessageControl(pJob->SessionID, pJob->pPacket);
				break;

			case dfJOBTYPE_JOIN:
				pServer->CreateCharacter(pJob->SessionID);
				break;

			case dfJOBTYPE_LEAVE:
				pServer->LeaveCharacter(pJob->SessionID);
				break;

			case dfJOBTYPE_HEARTBEAT:
				pServer->CheckHeartBeat();
				break;
			default:
				pServer->pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"JobType:%d", pJob->JobType);
				pServer->pLogger->Crash();
				break;
			}

			pServer->FreeJob(pJob);
		}
		else
		{
			DWORD SleepTime = timeGetTime();
			WaitForSingleObject(pServer->hJobEvent, INFINITE);
			SleepTime = timeGetTime() - SleepTime;
			InterlockedExchangeAdd(&pServer->UpdateThreadSleepTime, SleepTime);
			InterlockedIncrement(&pServer->UpdateThreadRunningTPS);
		}
	}


	return 0;
}


void Chat_Server::MessageControl(unsigned __int64 SessionID, CPacket* pMessagePacket)
{
	WORD MessageType;
	stCharacter* pCharacter;

	pCharacter = FindCharacter(SessionID);
	if (pCharacter == NULL)
	{
		pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# Character Not Exist #");
		pLogger->Crash();
	}

	pCharacter->dwLastRecvTime = timeGetTime();

	*pMessagePacket >> MessageType;

	switch (MessageType)
	{
	case en_PACKET_CS_CHAT_SERVER:
		break;

	case en_PACKET_CS_CHAT_REQ_LOGIN:
	{
		if (pCharacter->SectorX != -1)
		{
			pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# Character Already Exist #");
			pLogger->Crash();
		}

		*pMessagePacket >> pCharacter->AccountNo;
		pMessagePacket->GetData(pCharacter->ID, 20);
		pMessagePacket->GetData(pCharacter->Nickname, 20);
		BYTE Status;
		MessageType = en_PACKET_CS_CHAT_RES_LOGIN;

		pMessagePacket->Clear();

		//로그인 정보 확인 ... 성공 시
		Status = 1;

		////로그인 실패 시
		//Status = 0;

		*pMessagePacket << MessageType << Status << pCharacter->AccountNo;
		SendPacket(SessionID, pMessagePacket);

		//if (Status == 0)
		//{
		//	//세션 종료
		//}
	}
	break;

	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
	{
		INT64 AccountNo;
		WORD SectorX;
		WORD SectorY;

		*pMessagePacket >> AccountNo >> SectorX >> SectorY;

		if (pCharacter->AccountNo != AccountNo)
		{
			pLogger->Log(L"Disconnect", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
				AccountNo, pCharacter->AccountNo);
			KillSession(pCharacter->SessionID);
			break;
		}

		//Sector 변경 시 리스트에서 Pop, Push 필요
		if (SectorX >= 0 && SectorX < dfSECTOR_X_MAX && SectorY >= 0 && SectorY < dfSECTOR_Y_MAX)
		{
			if (pCharacter->SectorX != -1) // 기존에 섹터에 들어가있는경우 <> 로그인만 진행한 경우 -1로 초기화
			{
				std::list<stCharacter*>::iterator iter_SectorList;
				for (iter_SectorList = SectorList[pCharacter->SectorY][pCharacter->SectorX].begin();
					iter_SectorList != SectorList[pCharacter->SectorY][pCharacter->SectorX].end(); ++iter_SectorList)
				{
					if (*iter_SectorList == pCharacter)
					{
						SectorList[pCharacter->SectorY][pCharacter->SectorX].erase(iter_SectorList);
						break;
					}
				}
			}
			pCharacter->SectorX = SectorX;
			pCharacter->SectorY = SectorY;

			SectorList[SectorY][SectorX].push_back(pCharacter);
		}

		MessageType = en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
		pMessagePacket->Clear();
		*pMessagePacket << MessageType << pCharacter->AccountNo << pCharacter->SectorX << pCharacter->SectorY;
		SendPacket(SessionID, pMessagePacket);

	}
		break;

	case en_PACKET_CS_CHAT_REQ_MESSAGE:
	{
		INT64 AccountNo;
		WORD MessageLen;

		*pMessagePacket >> AccountNo >> MessageLen;
		if (pCharacter->AccountNo != AccountNo)
		{
			pLogger->Log(L"Disconnect", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
				AccountNo, pCharacter->AccountNo);
			KillSession(pCharacter->SessionID);
			break;
		}
		
		if (MessageLen >= dfMESSAGEBUFSIZE)
		{
			pLogger->Log(L"ASDF", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"Message Len:%d", MessageLen);
			pLogger->Crash();
		}

		pMessagePacket->GetData((char*)MessageBuf, MessageLen);
		MessageBuf[MessageLen / 2] = L'\0';

		MessageType = en_PACKET_CS_CHAT_RES_MESSAGE;
		pMessagePacket->Clear();

		*pMessagePacket << MessageType << pCharacter->AccountNo;
		pMessagePacket->PutData(pCharacter->ID, 20);
		pMessagePacket->PutData(pCharacter->Nickname, 20);

		*pMessagePacket << MessageLen;
		pMessagePacket->PutData((char*)MessageBuf, MessageLen);

		SendPacketAround(pCharacter->SectorX, pCharacter->SectorY, pMessagePacket);
	}
		break;


	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		break;

	default:
	{
		KillSession(SessionID);
		pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_SYSTEM, L"# MessageType Undefined # MessageType:%d, AccountNo:%d, ID:%s, Nickname:%s",
			MessageType, pCharacter->AccountNo, pCharacter->ID, pCharacter->Nickname);
	}
		break;
	}


	FreePacket(pMessagePacket);
}

Chat_Server::stCharacter* Chat_Server::CreateCharacter(unsigned __int64 SessionID)
{
	if (CharacterMap.find(SessionID) != CharacterMap.end()) return NULL;

	stCharacter* pCharacter = CharacterPool.Alloc();
	pCharacter->SessionID = SessionID;
	pCharacter->AccountNo = 0;
	pCharacter->SectorX = -1;
	pCharacter->dwLastRecvTime = timeGetTime();
	CharacterMap.insert(std::make_pair(SessionID, pCharacter));
	return pCharacter;
}


Chat_Server::stCharacter* Chat_Server::FindCharacter(unsigned __int64 SessionID)
{
	std::map<unsigned __int64, stCharacter*>::iterator iter_Find = CharacterMap.find(SessionID);
	if (iter_Find == CharacterMap.end()) return NULL;
	return iter_Find->second;
}


void Chat_Server::CheckHeartBeat(void)
{
	DWORD Cur_Time = timeGetTime();
	std::map<unsigned __int64, stCharacter*>::iterator iter_CharacterMap;
	for (iter_CharacterMap = CharacterMap.begin(); iter_CharacterMap != CharacterMap.end(); ++iter_CharacterMap)
	{
		stCharacter* pCharacter = iter_CharacterMap->second;

		if (Cur_Time - pCharacter->dwLastRecvTime > 40000)
		{
			pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_SYSTEM, L"# Character TimeOut # AccountNo:%d, ID:%s, Nickname:%s",
				pCharacter->AccountNo, pCharacter->ID, pCharacter->Nickname);
			KillSession(pCharacter->SessionID);
		}
	}
}

void Chat_Server::LeaveCharacter(unsigned __int64 SessionID)
{
	stCharacter* pCharacter = FindCharacter(SessionID);
	if (pCharacter != NULL)
	{
		CharacterMap.erase(SessionID);

		if (pCharacter->SectorX >= 0 && pCharacter->SectorY >= 0)
		{
			std::list<stCharacter*>::iterator iter_List;
			for (iter_List = SectorList[pCharacter->SectorY][pCharacter->SectorX].begin(); iter_List != SectorList[pCharacter->SectorY][pCharacter->SectorX].end(); ++iter_List)
			{
				if (pCharacter == *iter_List)
				{
					SectorList[pCharacter->SectorY][pCharacter->SectorX].erase(iter_List);
					break;
				}
			}
		}

		CharacterPool.Free(pCharacter);
	}
	else
	{
		pLogger->Log(L"Content", Logger::en_LOG_LEVEL::eLEVEL_ERROR, L"# LeaveSession # Character Not Exist");
		pLogger->Crash();
	}
}

void Chat_Server::SendPacketAround(int iSectorX, int iSectorY, CPacket* pPacket)
{
	int iCntX, iCntY;
	int TargetCnt = 0;

	iSectorX--;
	iSectorY--;

	for (iCntY = 0; iCntY < 3; iCntY++)
	{
		if (iSectorY + iCntY < 0 || iSectorY + iCntY >= dfSECTOR_Y_MAX) continue;

		for (iCntX = 0; iCntX < 3; iCntX++)
		{
			if (iSectorX + iCntX < 0 || iSectorX + iCntX >= dfSECTOR_X_MAX) continue;

			std::list<stCharacter*>::iterator iter_Sector;
			for (iter_Sector = SectorList[iSectorY + iCntY][iSectorX + iCntX].begin(); iter_Sector != SectorList[iSectorY + iCntY][iSectorX + iCntX].end(); ++iter_Sector)
			{
				SendPacket((*iter_Sector)->SessionID, pPacket);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////

//void Chat_Server::EnqueueJob(WORD JobType, unsigned __int64 SessionID, CPacket* pPacket)
//{
//	stJob* pJob = AllocJob();
//	pJob->JobType = JobType;
//	pJob->SessionID = SessionID;
//	pJob->pPacket = pPacket;
//
//	JobQueue.Enqueue(pJob);
//	SetEvent(hJobEvent);
//}

//Chat_Server::stJob* Chat_Server::DequeueJob(void)
//{
//	stJob* pJob;
//	while (1)
//	{
//		if (JobQueue.Dequeue(pJob))
//		{
//			InterlockedIncrement(&_JobTPS);
//			return pJob;
//		}
//
//		_Running_CurTime = timeGetTime() - _Running_CurTime;
//		InterlockedExchangeAdd(&_UpdateThread_RunningTime, _Running_CurTime);
//
//		WaitForSingleObject(hJobEvent, INFINITE);
//		InterlockedIncrement(&_UpdateThread_RunningTPS);
//
//		_Running_CurTime = timeGetTime();
//
//	}
//}
//
//
//void Chat_Server::DequeueJob(WORD& JobType, unsigned __int64& SessionID, CPacket*& pPacket)
//{
////	1.	while true 뭐임
////	2.	이 함수의 기능이 너무 많음 // 여기서 블록되면 안되지 + 여기서 _Running_CurTime 업데이트 하는것도 이상함
////	3.	타임 재는거 스타일이 좀....
//
//	while (1)
//	{
//		stJob* pJob;
//		if (JobQueue.Dequeue(pJob))
//		{
//			JobType = pJob->JobType;
//			SessionID = pJob->SessionID;
//			pPacket = pJob->pPacket;
//
//			FreeJob(pJob);
//
//			InterlockedIncrement(&_JobTPS);
//			return;
//		}
//
//		_Running_CurTime = timeGetTime() - _Running_CurTime;
//		InterlockedExchangeAdd(&_UpdateThread_RunningTime, _Running_CurTime);
//		WaitForSingleObject(hJobEvent, INFINITE);
//
//		InterlockedIncrement(&_UpdateThread_RunningTPS);
//		
//		_Running_CurTime = timeGetTime();
//	}
//}


//void Chat_Server::CreateContentThread(bool HeartBeat)
//{
//	hContentThread = (HANDLE)_beginthreadex(NULL, 0, UpdateThread_Chat_Field1, (LPVOID)this, 0, NULL);
//
//	if(HeartBeat) hTimerThread5000 = (HANDLE)_beginthreadex(NULL, 0, TimerThread_Chat_5000, (LPVOID)this, 0, NULL);
//}


Chat_Server::stJob* Chat_Server::AllocJob(int JobType, unsigned __int64 SessionID, CPacket* pPacket)
{
	MemoryPool_TLS_Node<stJob>* pJobPool = (MemoryPool_TLS_Node<stJob>*)TlsGetValue(MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTLSIndex());
	if (pJobPool == NULL)
	{
		pJobPool = new MemoryPool_TLS_Node<stJob>;
		pJobPool->SetTLS();
	}
	stJob* pJob = pJobPool->Alloc();
	pJob->JobType = JobType;
	pJob->SessionID = SessionID;
	pJob->pPacket = pPacket;

	return pJob;
}


void Chat_Server::FreeJob(stJob* pJob)
{
	MemoryPool_TLS_Node<stJob>* pJobPool = (MemoryPool_TLS_Node<stJob>*)TlsGetValue(MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTLSIndex());
	if (pJobPool == NULL)
	{
		pJobPool = new MemoryPool_TLS_Node<stJob>;
		pJobPool->SetTLS();
	}
	pJobPool->Free(pJob);
}