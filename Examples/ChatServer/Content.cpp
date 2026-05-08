//#include "CNet_Server.h"
#include "Content.h"
#include "ChatProtocol.h"

#include "LockFree_Queue_TLS.h"

constexpr int kMessageBufsize = 1000;
WCHAR MessageBuf[kMessageBufsize];

Chat_Server::Chat_Server(int MaxUser, bool HeartBeatFlag) {
    hContentThread = INVALID_HANDLE_VALUE;
    hTimerThread5000 = INVALID_HANDLE_VALUE;
    hJobEvent = CreateEvent(NULL, false, false, NULL);

    _MaxUser = MaxUser;

    _Running_CurTime = 0;

    _JobTPS = 0;
    UpdateThreadRunningTPS = 0;
    UpdateThreadSleepTime = 0;

    pLogger = Logger::GetInstance();

    hContentThread =
        (HANDLE)_beginthreadex(NULL, 0, UpdateThread_Chat_Field1, (LPVOID)this, 0, NULL);
    if (HeartBeatFlag)
        hTimerThread5000 =
            (HANDLE)_beginthreadex(NULL, 0, TimerThread_Chat_5000, (LPVOID)this, 0, NULL);
}


bool Chat_Server::OnConnectionRequest() {
    if (GetCharacterSize() < (int)_MaxUser)
        return true;
    else
        return false;
}

void Chat_Server::OnClientJoin(unsigned __int64 SessionID) {
    stJob* pJob = AllocJob(JobType::kJoin, SessionID, NULL);
    JobQueue.Enqueue(pJob);
    SetEvent(hJobEvent);
}
void Chat_Server::OnClientLeave(unsigned __int64 SessionID) {
    stJob* pJob = AllocJob(JobType::kLeave, SessionID, NULL);
    JobQueue.Enqueue(pJob);
    SetEvent(hJobEvent);
}


void Chat_Server::OnRecv(unsigned __int64 SessionID, CPacket* pPacket) {
    pPacket->IncrementRef();
    stJob* pJob = AllocJob(JobType::kMessage, SessionID, pPacket);
    JobQueue.Enqueue(pJob);
    SetEvent(hJobEvent);
}

unsigned WINAPI Chat_Server::TimerThread_Chat_5000(LPVOID lpThreadParameter) {
    Chat_Server* pServer = (Chat_Server*)lpThreadParameter;
    stJob* pJob;

    while (1) {
        pJob = pServer->AllocJob(JobType::kHeartbeat, NULL, NULL);
        pServer->JobQueue.Enqueue(pJob);
        SetEvent(pServer->hJobEvent);

        Sleep(5000);
    }

    return 0;
}


unsigned WINAPI Chat_Server::UpdateThread_Chat_Field1(LPVOID lpThreadParameter) {
    Chat_Server* pServer = (Chat_Server*)lpThreadParameter;
    stJob* pJob;

    while (1) {
        if (pServer->JobQueue.Dequeue(pJob)) {
            InterlockedIncrement(&pServer->_JobTPS);

            switch (pJob->type) {
                case JobType::kMessage:
                    pServer->MessageControl(pJob->SessionID, pJob->pPacket);
                    break;

                case JobType::kJoin:
                    pServer->CreateCharacter(pJob->SessionID);
                    break;

                case JobType::kLeave:
                    pServer->LeaveCharacter(pJob->SessionID);
                    break;

                case JobType::kHeartbeat:
                    pServer->CheckHeartBeat();
                    break;
                default:
                    pServer->pLogger->Log(L"Content", Logger::LogLevel::kError,
                                          L"JobType:%d", pJob->type);
                    pServer->pLogger->Crash();
                    break;
            }

            pServer->FreeJob(pJob);
        } else {
            DWORD SleepTime = timeGetTime();
            WaitForSingleObject(pServer->hJobEvent, INFINITE);
            SleepTime = timeGetTime() - SleepTime;
            InterlockedExchangeAdd(&pServer->UpdateThreadSleepTime, SleepTime);
            InterlockedIncrement(&pServer->UpdateThreadRunningTPS);
        }
    }


    return 0;
}


void Chat_Server::MessageControl(unsigned __int64 SessionID, CPacket* pMessagePacket) {
    WORD MessageType;
    stCharacter* pCharacter;

    pCharacter = FindCharacter(SessionID);
    if (pCharacter == NULL) {
        pLogger->Log(L"Content", Logger::LogLevel::kError, L"# Character Not Exist #");
        pLogger->Crash();
    }

    pCharacter->dwLastRecvTime = timeGetTime();

    *pMessagePacket >> MessageType;

    switch (MessageType) {
        case kCsChatServer:
            break;

        case kCsChatReqLogin: {
            if (pCharacter->SectorX != -1) {
                pLogger->Log(L"Content", Logger::LogLevel::kError,
                             L"# Character Already Exist #");
                pLogger->Crash();
            }

            *pMessagePacket >> pCharacter->AccountNo;
            pMessagePacket->GetData(pCharacter->ID, 20);
            pMessagePacket->GetData(pCharacter->Nickname, 20);
            BYTE Status;
            MessageType = kCsChatResLogin;

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
        } break;

        case kCsChatReqSectorMove: {
            INT64 AccountNo;
            WORD SectorX;
            WORD SectorY;

            *pMessagePacket >> AccountNo >> SectorX >> SectorY;

            if (pCharacter->AccountNo != AccountNo) {
                pLogger->Log(
                    L"Disconnect", Logger::LogLevel::kError,
                    L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
                    AccountNo, pCharacter->AccountNo);
                KillSession(pCharacter->SessionID);
                break;
            }

            //Sector 변경 시 리스트에서 Pop, Push 필요
            if (SectorX >= 0 && SectorX < kSectorXMax && SectorY >= 0 &&
                SectorY < kSectorYMax) {
                if (pCharacter->SectorX !=
                    -1)  // 기존에 섹터에 들어가있는경우 <> 로그인만 진행한 경우 -1로 초기화
                {
                    std::list<stCharacter*>::iterator iter_SectorList;
                    for (iter_SectorList =
                             SectorList[pCharacter->SectorY][pCharacter->SectorX].begin();
                         iter_SectorList !=
                         SectorList[pCharacter->SectorY][pCharacter->SectorX].end();
                         ++iter_SectorList) {
                        if (*iter_SectorList == pCharacter) {
                            SectorList[pCharacter->SectorY][pCharacter->SectorX].erase(
                                iter_SectorList);
                            break;
                        }
                    }
                }
                pCharacter->SectorX = SectorX;
                pCharacter->SectorY = SectorY;

                SectorList[SectorY][SectorX].push_back(pCharacter);
            }

            MessageType = kCsChatResSectorMove;
            pMessagePacket->Clear();
            *pMessagePacket << MessageType << pCharacter->AccountNo << pCharacter->SectorX
                            << pCharacter->SectorY;
            SendPacket(SessionID, pMessagePacket);

        } break;

        case kCsChatReqMessage: {
            INT64 AccountNo;
            WORD MessageLen;

            *pMessagePacket >> AccountNo >> MessageLen;
            if (pCharacter->AccountNo != AccountNo) {
                pLogger->Log(
                    L"Disconnect", Logger::LogLevel::kError,
                    L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
                    AccountNo, pCharacter->AccountNo);
                KillSession(pCharacter->SessionID);
                break;
            }

            if (MessageLen >= kMessageBufsize) {
                pLogger->Log(L"ASDF", Logger::LogLevel::kError, L"Message Len:%d",
                             MessageLen);
                pLogger->Crash();
            }

            pMessagePacket->GetData((char*)MessageBuf, MessageLen);
            MessageBuf[MessageLen / 2] = L'\0';

            MessageType = kCsChatResMessage;
            pMessagePacket->Clear();

            *pMessagePacket << MessageType << pCharacter->AccountNo;
            pMessagePacket->PutData(pCharacter->ID, 20);
            pMessagePacket->PutData(pCharacter->Nickname, 20);

            *pMessagePacket << MessageLen;
            pMessagePacket->PutData((char*)MessageBuf, MessageLen);

            SendPacketAround(pCharacter->SectorX, pCharacter->SectorY, pMessagePacket);
        } break;


        case kCsChatReqHeartbeat:
            break;

        default: {
            KillSession(SessionID);
            pLogger->Log(
                L"Content", Logger::LogLevel::kSystem,
                L"# MessageType Undefined # MessageType:%d, AccountNo:%d, ID:%s, Nickname:%s",
                MessageType, pCharacter->AccountNo, pCharacter->ID, pCharacter->Nickname);
        } break;
    }


    FreePacket(pMessagePacket);
}

Chat_Server::stCharacter* Chat_Server::CreateCharacter(unsigned __int64 SessionID) {
    if (CharacterMap.find(SessionID) != CharacterMap.end())
        return NULL;

    stCharacter* pCharacter = CharacterPool.Alloc();
    pCharacter->SessionID = SessionID;
    pCharacter->AccountNo = 0;
    pCharacter->SectorX = -1;
    pCharacter->dwLastRecvTime = timeGetTime();
    CharacterMap.insert(std::make_pair(SessionID, pCharacter));
    return pCharacter;
}


Chat_Server::stCharacter* Chat_Server::FindCharacter(unsigned __int64 SessionID) {
    std::map<unsigned __int64, stCharacter*>::iterator iter_Find = CharacterMap.find(SessionID);
    if (iter_Find == CharacterMap.end())
        return NULL;
    return iter_Find->second;
}


void Chat_Server::CheckHeartBeat(void) {
    DWORD Cur_Time = timeGetTime();
    std::map<unsigned __int64, stCharacter*>::iterator iter_CharacterMap;
    for (iter_CharacterMap = CharacterMap.begin(); iter_CharacterMap != CharacterMap.end();
         ++iter_CharacterMap) {
        stCharacter* pCharacter = iter_CharacterMap->second;

        if (Cur_Time - pCharacter->dwLastRecvTime > 40000) {
            pLogger->Log(L"Content", Logger::LogLevel::kSystem,
                         L"# Character TimeOut # AccountNo:%d, ID:%s, Nickname:%s",
                         pCharacter->AccountNo, pCharacter->ID, pCharacter->Nickname);
            KillSession(pCharacter->SessionID);
        }
    }
}

void Chat_Server::LeaveCharacter(unsigned __int64 SessionID) {
    stCharacter* pCharacter = FindCharacter(SessionID);
    if (pCharacter != NULL) {
        CharacterMap.erase(SessionID);

        if (pCharacter->SectorX >= 0 && pCharacter->SectorY >= 0) {
            std::list<stCharacter*>::iterator iter_List;
            for (iter_List = SectorList[pCharacter->SectorY][pCharacter->SectorX].begin();
                 iter_List != SectorList[pCharacter->SectorY][pCharacter->SectorX].end();
                 ++iter_List) {
                if (pCharacter == *iter_List) {
                    SectorList[pCharacter->SectorY][pCharacter->SectorX].erase(iter_List);
                    break;
                }
            }
        }

        CharacterPool.Free(pCharacter);
    } else {
        pLogger->Log(L"Content", Logger::LogLevel::kError,
                     L"# LeaveSession # Character Not Exist");
        pLogger->Crash();
    }
}

void Chat_Server::SendPacketAround(int iSectorX, int iSectorY, CPacket* pPacket) {
    int iCntX, iCntY;
    int TargetCnt = 0;

    iSectorX--;
    iSectorY--;

    for (iCntY = 0; iCntY < 3; iCntY++) {
        if (iSectorY + iCntY < 0 || iSectorY + iCntY >= kSectorYMax)
            continue;

        for (iCntX = 0; iCntX < 3; iCntX++) {
            if (iSectorX + iCntX < 0 || iSectorX + iCntX >= kSectorXMax)
                continue;

            std::list<stCharacter*>::iterator iter_Sector;
            for (iter_Sector = SectorList[iSectorY + iCntY][iSectorX + iCntX].begin();
                 iter_Sector != SectorList[iSectorY + iCntY][iSectorX + iCntX].end();
                 ++iter_Sector) {
                SendPacket((*iter_Sector)->SessionID, pPacket);
            }
        }
    }
}



Chat_Server::stJob* Chat_Server::AllocJob(JobType type, unsigned __int64 SessionID,
                                          CPacket* pPacket) {
    MemoryPool_TLS_Node<stJob>* pJobPool = (MemoryPool_TLS_Node<stJob>*)TlsGetValue(
        MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTLSIndex());
    if (pJobPool == NULL) {
        pJobPool = new MemoryPool_TLS_Node<stJob>;
        pJobPool->SetTLS();
    }
    stJob* pJob = pJobPool->Alloc();
    pJob->type = type;
    pJob->SessionID = SessionID;
    pJob->pPacket = pPacket;

    return pJob;
}


void Chat_Server::FreeJob(stJob* pJob) {
    MemoryPool_TLS_Node<stJob>* pJobPool = (MemoryPool_TLS_Node<stJob>*)TlsGetValue(
        MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTLSIndex());
    if (pJobPool == NULL) {
        pJobPool = new MemoryPool_TLS_Node<stJob>;
        pJobPool->SetTLS();
    }
    pJobPool->Free(pJob);
}