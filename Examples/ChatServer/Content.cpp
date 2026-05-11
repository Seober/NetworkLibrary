//#include "NetServer.h"
#include "Content.h"
#include "ChatProtocol.h"

#include "LockFreeQueue.h"

constexpr int kMessageBufsize = 1000;
WCHAR MessageBuf[kMessageBufsize];

ChatServer::ChatServer(int maxUser, bool heartBeatFlag) {
    ContentThread = INVALID_HANDLE_VALUE;
    TimerThread5000 = INVALID_HANDLE_VALUE;
    JobEvent = CreateEvent(NULL, false, false, NULL);

    MaxUser = maxUser;

    RunningCurTime = 0;

    JobTPS = 0;
    UpdateThreadRunningTPS = 0;
    UpdateThreadSleepTime = 0;

    pLogger = Logger::GetInstance();

    ContentThread =
        (HANDLE)_beginthreadex(NULL, 0, UpdateThreadFunc, (LPVOID)this, 0, NULL);
    if (heartBeatFlag)
        TimerThread5000 =
            (HANDLE)_beginthreadex(NULL, 0, HeartbeatTimerThread, (LPVOID)this, 0, NULL);
}


bool ChatServer::OnConnectionRequest() {
    if (GetCharacterSize() < (int)MaxUser)
        return true;
    else
        return false;
}

void ChatServer::OnClientJoin(unsigned __int64 sessionID) {
    Job* job = AllocJob(JobType::kJoin, sessionID, NULL);
    JobQueue.Enqueue(job);
    SetEvent(JobEvent);
}
void ChatServer::OnClientLeave(unsigned __int64 sessionID) {
    Job* job = AllocJob(JobType::kLeave, sessionID, NULL);
    JobQueue.Enqueue(job);
    SetEvent(JobEvent);
}


void ChatServer::OnRecv(unsigned __int64 sessionID, Packet* packet) {
    packet->IncrementRef();
    Job* job = AllocJob(JobType::kMessage, sessionID, packet);
    JobQueue.Enqueue(job);
    SetEvent(JobEvent);
}

unsigned WINAPI ChatServer::HeartbeatTimerThread(LPVOID lpThreadParameter) {
    ChatServer* server = (ChatServer*)lpThreadParameter;
    Job* job;

    while (1) {
        job = server->AllocJob(JobType::kHeartbeat, NULL, NULL);
        server->JobQueue.Enqueue(job);
        SetEvent(server->JobEvent);

        Sleep(5000);
    }

    return 0;
}


unsigned WINAPI ChatServer::UpdateThreadFunc(LPVOID lpThreadParameter) {
    ChatServer* server = (ChatServer*)lpThreadParameter;
    Job* job;

    while (1) {
        if (server->JobQueue.Dequeue(job)) {
            InterlockedIncrement(&server->JobTPS);

            switch (job->type) {
                case JobType::kMessage:
                    server->MessageControl(job->SessionID, job->packet);
                    break;

                case JobType::kJoin:
                    server->CreateCharacter(job->SessionID);
                    break;

                case JobType::kLeave:
                    server->LeaveCharacter(job->SessionID);
                    break;

                case JobType::kHeartbeat:
                    server->CheckHeartBeat();
                    break;
                default:
                    server->pLogger->Log(L"Content", Logger::LogLevel::kError,
                                          L"JobType:%d", job->type);
                    server->pLogger->Crash();
                    break;
            }

            server->FreeJob(job);
        } else {
            DWORD sleepTime = timeGetTime();
            WaitForSingleObject(server->JobEvent, INFINITE);
            sleepTime = timeGetTime() - sleepTime;
            InterlockedExchangeAdd(&server->UpdateThreadSleepTime, sleepTime);
            InterlockedIncrement(&server->UpdateThreadRunningTPS);
        }
    }


    return 0;
}


void ChatServer::MessageControl(unsigned __int64 sessionID, Packet* messagePacket) {
    WORD messageType;
    Character* character;

    character = FindCharacter(sessionID);
    if (character == NULL) {
        pLogger->Log(L"Content", Logger::LogLevel::kError, L"# Character Not Exist #");
        pLogger->Crash();
    }

    character->LastRecvTime = timeGetTime();

    *messagePacket >> messageType;

    switch (messageType) {
        case kCsChatServer:
            break;

        case kCsChatReqLogin: {
            if (character->SectorX != -1) {
                pLogger->Log(L"Content", Logger::LogLevel::kError,
                             L"# Character Already Exist #");
                pLogger->Crash();
            }

            *messagePacket >> character->AccountNo;
            messagePacket->GetData(character->ID, 20);
            messagePacket->GetData(character->Nickname, 20);
            BYTE status;
            messageType = kCsChatResLogin;

            messagePacket->Clear();

            //로그인 정보 확인 ... 성공 시
            status = 1;

            ////로그인 실패 시
            //status = 0;

            *messagePacket << messageType << status << character->AccountNo;
            SendPacket(sessionID, messagePacket);

            //if (status == 0)
            //{
            //	//세션 종료
            //}
        } break;

        case kCsChatReqSectorMove: {
            INT64 accountNo;
            WORD sectorX;
            WORD sectorY;

            *messagePacket >> accountNo >> sectorX >> sectorY;

            if (character->AccountNo != accountNo) {
                pLogger->Log(
                    L"Disconnect", Logger::LogLevel::kError,
                    L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
                    accountNo, character->AccountNo);
                KillSession(character->SessionID);
                break;
            }

            //Sector 변경 시 리스트에서 Pop, Push 필요
            if (sectorX >= 0 && sectorX < kSectorXMax && sectorY >= 0 &&
                sectorY < kSectorYMax) {
                if (character->SectorX !=
                    -1)  // 기존에 섹터에 들어가있는경우 <> 로그인만 진행한 경우 -1로 초기화
                {
                    std::list<Character*>::iterator iter_SectorList;
                    for (iter_SectorList =
                             SectorList[character->SectorY][character->SectorX].begin();
                         iter_SectorList !=
                         SectorList[character->SectorY][character->SectorX].end();
                         ++iter_SectorList) {
                        if (*iter_SectorList == character) {
                            SectorList[character->SectorY][character->SectorX].erase(
                                iter_SectorList);
                            break;
                        }
                    }
                }
                character->SectorX = sectorX;
                character->SectorY = sectorY;

                SectorList[sectorY][sectorX].push_back(character);
            }

            messageType = kCsChatResSectorMove;
            messagePacket->Clear();
            *messagePacket << messageType << character->AccountNo << character->SectorX
                            << character->SectorY;
            SendPacket(sessionID, messagePacket);

        } break;

        case kCsChatReqMessage: {
            INT64 accountNo;
            WORD messageLen;

            *messagePacket >> accountNo >> messageLen;
            if (character->AccountNo != accountNo) {
                pLogger->Log(
                    L"Disconnect", Logger::LogLevel::kError,
                    L"# SectorMove # KillSession Called > AccountNo:%d, CharacterAccountNo:%d",
                    accountNo, character->AccountNo);
                KillSession(character->SessionID);
                break;
            }

            if (messageLen >= kMessageBufsize) {
                pLogger->Log(L"ASDF", Logger::LogLevel::kError, L"Message Len:%d",
                             messageLen);
                pLogger->Crash();
            }

            messagePacket->GetData((char*)MessageBuf, messageLen);
            MessageBuf[messageLen / 2] = L'\0';

            messageType = kCsChatResMessage;
            messagePacket->Clear();

            *messagePacket << messageType << character->AccountNo;
            messagePacket->PutData(character->ID, 20);
            messagePacket->PutData(character->Nickname, 20);

            *messagePacket << messageLen;
            messagePacket->PutData((char*)MessageBuf, messageLen);

            SendPacketAround(character->SectorX, character->SectorY, messagePacket);
        } break;


        case kCsChatReqHeartbeat:
            break;

        default: {
            KillSession(sessionID);
            pLogger->Log(
                L"Content", Logger::LogLevel::kSystem,
                L"# messageType Undefined # messageType:%d, AccountNo:%d, ID:%s, Nickname:%s",
                messageType, character->AccountNo, character->ID, character->Nickname);
        } break;
    }


    FreePacket(messagePacket);
}

ChatServer::Character* ChatServer::CreateCharacter(unsigned __int64 sessionID) {
    if (CharacterMap.find(sessionID) != CharacterMap.end())
        return NULL;

    Character* character = CharacterPool.Alloc();
    character->SessionID = sessionID;
    character->AccountNo = 0;
    character->SectorX = -1;
    character->LastRecvTime = timeGetTime();
    CharacterMap.insert(std::make_pair(sessionID, character));
    return character;
}


ChatServer::Character* ChatServer::FindCharacter(unsigned __int64 sessionID) {
    std::map<unsigned __int64, Character*>::iterator iter_Find = CharacterMap.find(sessionID);
    if (iter_Find == CharacterMap.end())
        return NULL;
    return iter_Find->second;
}


void ChatServer::CheckHeartBeat(void) {
    DWORD curTime = timeGetTime();
    std::map<unsigned __int64, Character*>::iterator iter_CharacterMap;
    for (iter_CharacterMap = CharacterMap.begin(); iter_CharacterMap != CharacterMap.end();
         ++iter_CharacterMap) {
        Character* character = iter_CharacterMap->second;

        if (curTime - character->LastRecvTime > 40000) {
            pLogger->Log(L"Content", Logger::LogLevel::kSystem,
                         L"# Character TimeOut # AccountNo:%d, ID:%s, Nickname:%s",
                         character->AccountNo, character->ID, character->Nickname);
            KillSession(character->SessionID);
        }
    }
}

void ChatServer::LeaveCharacter(unsigned __int64 sessionID) {
    Character* character = FindCharacter(sessionID);
    if (character != NULL) {
        CharacterMap.erase(sessionID);

        if (character->SectorX >= 0 && character->SectorY >= 0) {
            std::list<Character*>::iterator iter_List;
            for (iter_List = SectorList[character->SectorY][character->SectorX].begin();
                 iter_List != SectorList[character->SectorY][character->SectorX].end();
                 ++iter_List) {
                if (character == *iter_List) {
                    SectorList[character->SectorY][character->SectorX].erase(iter_List);
                    break;
                }
            }
        }

        CharacterPool.Free(character);
    } else {
        pLogger->Log(L"Content", Logger::LogLevel::kError,
                     L"# LeaveSession # Character Not Exist");
        pLogger->Crash();
    }
}

void ChatServer::SendPacketAround(int sectorX, int sectorY, Packet* packet) {
    int cntX, cntY;
    int targetCnt = 0;

    sectorX--;
    sectorY--;

    for (cntY = 0; cntY < 3; cntY++) {
        if (sectorY + cntY < 0 || sectorY + cntY >= kSectorYMax)
            continue;

        for (cntX = 0; cntX < 3; cntX++) {
            if (sectorX + cntX < 0 || sectorX + cntX >= kSectorXMax)
                continue;

            std::list<Character*>::iterator iter_Sector;
            for (iter_Sector = SectorList[sectorY + cntY][sectorX + cntX].begin();
                 iter_Sector != SectorList[sectorY + cntY][sectorX + cntX].end();
                 ++iter_Sector) {
                SendPacket((*iter_Sector)->SessionID, packet);
            }
        }
    }
}



ChatServer::Job* ChatServer::AllocJob(JobType type, unsigned __int64 sessionID,
                                          Packet* packet) {
    TLSNodeMemoryPool<Job>* jobPool = (TLSNodeMemoryPool<Job>*)TlsGetValue(
        TLSChunkMemoryPool<Job>::GetInstance()->GetTLSIndex());
    if (jobPool == NULL) {
        jobPool = new TLSNodeMemoryPool<Job>;
        jobPool->SetTLS();
    }
    Job* job = jobPool->Alloc();
    job->type = type;
    job->SessionID = sessionID;
    job->packet = packet;

    return job;
}


void ChatServer::FreeJob(Job* job) {
    TLSNodeMemoryPool<Job>* jobPool = (TLSNodeMemoryPool<Job>*)TlsGetValue(
        TLSChunkMemoryPool<Job>::GetInstance()->GetTLSIndex());
    if (jobPool == NULL) {
        jobPool = new TLSNodeMemoryPool<Job>;
        jobPool->SetTLS();
    }
    jobPool->Free(job);
}