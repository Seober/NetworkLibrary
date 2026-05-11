#include "NetClient.h"


#include "TLSMemoryPool.h"
#include "LockFreeStack.h"
#include "Logger.h"
#include "XorPacketEncoder.h"

namespace {
constexpr BYTE kDefaultHeaderCode = 0x77;
constexpr BYTE kDefaultEncryptKey = 0x32;
}  // namespace

////////////////////////////////////////////////////////////////////////////////////////////////

unsigned WINAPI NetClient::WorkerThread(LPVOID lpThreadParameter) {
    Logger* pLogger = Logger::GetInstance();
    TLSNodeMemoryPool<Packet> packetPool;
    packetPool.SetTLS();

    NetClient* client = (NetClient*)lpThreadParameter;

    DWORD threadID = GetCurrentThreadId();

    DWORD transferred;
    NetClient::Session* targetSession;
    OVERLAPPED* tmpOverlapped;

    while (1) {
        GetQueuedCompletionStatus(client->IOCP, &transferred, (PULONG_PTR)&targetSession,
                                  &tmpOverlapped, INFINITE);

        if (transferred == 0 && targetSession == NULL && tmpOverlapped == NULL) {
            if (client->Shutdown)
                break;
            pLogger->Log(L"Network", Logger::LogLevel::kSystem, L"# GQCS return NULL");
            pLogger->Crash();
            break;
        }

        if (transferred != 0) {
            // Recv 완료통지
            if (&targetSession->RecvOverlapped == tmpOverlapped) {
                client->AddRecvBytes(transferred);
                targetSession->RecvQ.MoveWritePos(transferred);


                while (1) {
                    if (!client->CheckPacketMessageComplete(targetSession->SessionID,
                                                             &targetSession->RecvQ))
                        break;

                    Packet* packet = client->AllocPacket();
                    if (client->GetPacketMessage(packet, &targetSession->RecvQ) ==
                        false)  // Decode Fail, Need to Disconnect
                    {
                        client->FreePacket(packet);
                        client->KillSession(targetSession->SessionID);

                        WCHAR ipBuf[32];
                        pLogger->Log(
                            L"NetClient", Logger::LogLevel::kSystem,
                            L"Packet Decode Failed, sessionID:%p, IP:%s", targetSession->SessionID,
                            InetNtop(AF_INET, &targetSession->ServerAddr.sin_addr, ipBuf, 32));
                        break;
                    }
                    client->AddRecvPacket();
                    client->OnRecv(targetSession->SessionID, packet);

                    client->FreePacket(packet);
                }
                int remainSize = targetSession->RecvQ.GetDataSize();
                if (remainSize)
                    targetSession->RecvQ.ShiftDataToFront();
                else
                    targetSession->RecvQ.Clear();

                client->RecvPost(targetSession);
            }

            // Send 완료통지
            else if (&targetSession->SendOverlapped == tmpOverlapped) {
                client->AddSend(targetSession->SendPacketCnt, transferred);

                int tmpPacketCnt = targetSession->SendPacketCnt;
                for (int i = 0; i < tmpPacketCnt; i++) {
                    Packet* packet = targetSession->SendBuffer[i];
                    client->FreePacket(packet);
                }
                targetSession->SendPacketCnt = 0;

                targetSession->SendFlag = 0;

                targetSession->IncrementSessionRef();
                if (!client->SendPost(targetSession))
                    targetSession->DecrementSessionRef();
            }

            // SendPacket
            else if (tmpOverlapped == (LPOVERLAPPED)0xfffffffffffffffe) {
                targetSession->IncrementSessionRef();
                if (!client->SendPost(targetSession))
                    targetSession->DecrementSessionRef();
            }


            else {
                pLogger->Log(L"NetClient", Logger::LogLevel::kError,
                             L"Overlapped Pointer Error, sessionID:%p, UseFlag:%d, DeleteFlag:%d, "
                             L"SessionRef:%d",
                             targetSession->SessionID, targetSession->SessionUseFlag,
                             targetSession->ReleaseArr[0], targetSession->ReleaseArr[1]);
                pLogger->Crash();
            }
        }

        if (targetSession->DecrementSessionRef() == 0)
            client->DisconnectSession(targetSession);
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

NetClient::NetClient(IPacketEncoder* encoder) : Encoder(encoder), OwnsEncoder(false) {
    if (Encoder == nullptr) {
        Encoder = new XorPacketEncoder(kDefaultHeaderCode, kDefaultEncryptKey);
        OwnsEncoder = true;
    }

    IOCP = NULL;
    Shutdown = 0;

    SessionArr = NULL;
    TotalSessionCnt = 0;

    SessionIDCnt = 0;

    WorkerThreadID = NULL;
    WorkerThread_ = NULL;
    ThreadCnt = 0;

    PacketPool = TLSChunkMemoryPool<Packet>::GetInstance();

    InitializeSRWLock(&srwLogTransmitMap);
}

NetClient::~NetClient() {
    Stop();   // idempotent — 사용자가 명시적 Stop 안 했어도 안전

    // Stop이 thread 종료까지 보장. 여기서 handle/메모리 cleanup
    if (WorkerThread_) {
        for (int i = 0; i < ThreadCnt; i++)
            if (WorkerThread_[i])
                CloseHandle(WorkerThread_[i]);
        delete[] WorkerThread_;
        WorkerThread_ = NULL;
    }
    if (WorkerThreadID) {
        delete[] WorkerThreadID;
        WorkerThreadID = NULL;
    }

    if (TotalSessionCnt) {
        delete[] SessionArr;
        SessionArr = NULL;
        TotalSessionCnt = 0;
    }

    WSACleanup();

    if (OwnsEncoder) {
        delete Encoder;
        Encoder = nullptr;
    }
}

void NetClient::Stop() {
    if (InterlockedExchange(&Shutdown, 1) == 1)
        return;   // idempotent

    Logger* pLogger = Logger::GetInstance();

    // 1. 모든 active session → KillSession (CancelIoEx 트리거)
    //    Worker thread가 cancelled IO completion 받고 OnDisconnect 콜백 호출 + ReleaseSession
    for (u_short i = 0; i < TotalSessionCnt; i++) {
        Session* session = &SessionArr[i];
        if (session->ReleaseArr[0] == FALSE) {
            KillSession(session->SessionID);
        }
    }

    // 2. Polling wait — 모든 session FreeSessionStack 환원 대기 (timeout 5s)
    const DWORD timeoutMs = 5000;
    DWORD startTick = GetTickCount();
    while (GetConnectedSessionCnt() > 0) {
        if (GetTickCount() - startTick > timeoutMs) {
            pLogger->Log(L"NetClient", Logger::LogLevel::kError,
                         L"Stop timeout — %d sessions remaining (OnDisconnect callback skipped)",
                         GetConnectedSessionCnt());
            break;
        }
        Sleep(10);
    }

    // 3. WorkerThread 종료 — PQCS × ThreadCnt로 GQCS 깨움
    for (int i = 0; i < ThreadCnt; i++)
        PostQueuedCompletionStatus(IOCP, 0, 0, NULL);
    if (WorkerThread_ && ThreadCnt > 0)
        WaitForMultipleObjects(ThreadCnt, WorkerThread_, TRUE, INFINITE);

    // 4. IOCP close
    if (IOCP) {
        CloseHandle(IOCP);
        IOCP = NULL;
    }
}

bool NetClient::Start(u_short workerThreadCntTotal, u_short workerThreadCntRun,
                      u_short maxSessionCnt) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, workerThreadCntRun);
    if (IOCP == NULL)
        return false;

    //////////////////////////////////////////

    TotalSessionCnt = maxSessionCnt;
    SessionArr = new Session[TotalSessionCnt];
    for (u_short init_SessionArr = 0; init_SessionArr < TotalSessionCnt; init_SessionArr++) {
        SessionArr[init_SessionArr].ReleaseArr[0] = TRUE;
        SessionArr[init_SessionArr].ReleaseArr[1] = 0;
        SessionArr[init_SessionArr].SessionID = (unsigned __int64)init_SessionArr << 48;

        FreeSessionStack.Push(&SessionArr[init_SessionArr]);
    }

    ThreadCnt = workerThreadCntTotal;
    WorkerThreadID = new DWORD[ThreadCnt];
    WorkerThread_ = new HANDLE[ThreadCnt];

    for (int i = 0; i < ThreadCnt; i++) {
        WorkerThread_[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0,
                                                  (unsigned int*)&WorkerThreadID[i]);
        if (WorkerThread_[i] == NULL)
            return false;
    }

    return true;
}

unsigned __int64 NetClient::Connect(const WCHAR* serverIP, u_short serverPort) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return 0;

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    if (InetPton(AF_INET, serverIP, &serverAddr.sin_addr) != 1) {
        closesocket(sock);
        return 0;
    }

    if (connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        return 0;
    }

    Session* session = GetFreeSession();
    if (session == NULL) {
        closesocket(sock);
        return 0;
    }

    session->Socket = sock;
    session->ServerAddr = serverAddr;

    CreateIoCompletionPort((HANDLE)session->Socket, IOCP, (ULONG_PTR)session, 0);

    OnConnect(session->SessionID);

    RecvPost(session);


    if (session->DecrementSessionRef() == 0)
        DisconnectSession(session);

    return session->SessionID;
}

void NetClient::SendPacket(unsigned __int64 sessionID, Packet* packet) {
    Session* session = FindSession(sessionID);

    session->IncrementSessionRef();
    if (session->ReleaseArr[0] == FALSE && session->SessionID == sessionID) {
        packet->IncrementRef();

        Encoder->Encode(*packet);
        session->SendQ.Enqueue(packet);


        PostQueuedCompletionStatus(IOCP, 1, (ULONG_PTR)session,
                                   (LPOVERLAPPED)0xfffffffffffffffe);
    } else
        session->DecrementSessionRef();
}

bool NetClient::SendPost(Session* session, DWORD flag) {
    if (session->SessionUseFlag == false)
        return false;
    if (InterlockedExchange(&session->SendFlag, 1) == 1)
        return false;

    int packetCnt = session->SendQ.GetUseSize();
    while (!packetCnt) {
        session->SendFlag = 0;

        if (session->SendQ.GetUseSize()) {
            if (InterlockedExchange(&session->SendFlag, 1) == 0) {
                packetCnt = session->SendQ.GetUseSize();
            } else
                return false;
        } else
            return false;
    }

    int tmpPacketCnt = packetCnt;

    WSABUF wsabuf[1000];
    int wsabufCnt = 0;
    Packet* tmpPacket;
    while (packetCnt--) {
        if (session->SendQ.Dequeue(tmpPacket) == false) {
            Logger* pLogger = Logger::GetInstance();
            pLogger->Log(
                L"NetClient", Logger::LogLevel::kError,
                L"SendQ->Dequeue Failed, sessionID:%p, tmpRemainCnt:%d, SendQRemainCnt:%d,",
                session->SessionID, packetCnt, session->SendQ.GetUseSize());
            pLogger->Crash();
        }

        wsabuf[wsabufCnt].buf = tmpPacket->GetReadBufferPtr();
        wsabuf[wsabufCnt].len = tmpPacket->GetDataSize();
        session->SendBuffer[wsabufCnt] = tmpPacket;
        wsabufCnt++;
    }

    session->SendPacketCnt = wsabufCnt;

    int retval_WSASend =
        WSASend(session->Socket, wsabuf, wsabufCnt, NULL, flag, &session->SendOverlapped, NULL);
    if (retval_WSASend == SOCKET_ERROR) {
        int Err_Code = WSAGetLastError();
        switch (Err_Code) {
            case WSA_IO_PENDING:
                break;
            case 10004:
            case 10022:
            case 10053:
            case 10054:  // 피어별 연결 재설정
                return false;
            default: {
                Logger* pLogger = Logger::GetInstance();
                pLogger->Log(L"NetClient", Logger::LogLevel::kError,
                             L"# WSASend Func Err # ErrCode:%d, sessionID:%p, UseFlag:%d, "
                             L"DeleteFlag:%d, SessionRef:%d",
                             Err_Code, session->SessionID, session->SessionUseFlag,
                             session->ReleaseArr[0], session->ReleaseArr[1]);
                pLogger->Crash();
            }
                return false;
        }
    }

    if (session->SessionUseFlag == false)
        CancelIoEx((HANDLE)session->Socket, NULL);
    return true;
}


bool NetClient::RecvPost(Session* session, DWORD flag) {
    if (session->SessionUseFlag == false)
        return false;

    WSABUF wsabuf;
    wsabuf.buf = session->RecvQ.GetWriteBufferPtr();
    wsabuf.len = session->RecvQ.GetFreeSize();

    session->IncrementSessionRef();
    int retval_WSARecv =
        WSARecv(session->Socket, &wsabuf, 1, NULL, &flag, &session->RecvOverlapped, NULL);
    if (retval_WSARecv == SOCKET_ERROR) {
        int Err_Code = WSAGetLastError();
        switch (Err_Code) {
            case WSA_IO_PENDING:
                break;
            case 10004:
            case 10022:
            case 10053:
            case 10054:  // 피어별 연결 재설정
                session->DecrementSessionRef();
                return false;
                break;
            default: {
                Logger* pLogger = Logger::GetInstance();
                pLogger->Log(L"NetClient", Logger::LogLevel::kError,
                             L"# WSARecv Func Err # ErrCode:%d, sessionID:%p, UseFlag:%d, "
                             L"DeleteFlag:%d, SessionRef:%d",
                             Err_Code, session->SessionID, session->SessionUseFlag,
                             session->ReleaseArr[0], session->ReleaseArr[1]);
                pLogger->Crash();
            } break;
        }
    }
    if (session->SessionUseFlag == false)
        CancelIoEx((HANDLE)session->Socket, NULL);
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////


NetClient::Session* NetClient::GetFreeSession() {
    Session* session;
    FreeSessionStack.Pop(session);
    if (session == NULL)
        return NULL;

    InitSession(session);

    return session;
}

void NetClient::ReleaseSession(Session* session) {
    session->SessionID &= 0xffff000000000000;
    closesocket(session->Socket);

    int remainSize = session->SendQ.GetUseSize();
    Packet* packet;
    while (session->SendQ.Dequeue(packet))
        FreePacket(packet);

    remainSize = session->SendPacketCnt;
    for (int i = 0; i < remainSize; i++)
        FreePacket(session->SendBuffer[i]);
    session->SendPacketCnt = 0;

    FreeSessionStack.Push(session);
}

void NetClient::InitSession(Session* session) {
    session->SessionUseFlag = true;
    session->SessionID |= GenerateSessionID();

    ZeroMemory(&session->SendOverlapped, sizeof(session->SendOverlapped));
    ZeroMemory(&session->RecvOverlapped, sizeof(session->RecvOverlapped));

    session->SendFlag = 0;
    session->SendPacketCnt = 0;

    session->RecvQ.Clear();
    session->SendQ.Clear();

    session->IncrementSessionRef();
    session->ReleaseArr[0] = FALSE;
}

void NetClient::KillSession(unsigned __int64 sessionID) {
    Session* session = FindSession(sessionID);
    session->IncrementSessionRef();
    if (session->ReleaseArr[0] == FALSE && session->SessionID == sessionID) {
        session->SessionUseFlag = false;
        CancelIoEx((HANDLE)session->Socket, NULL);
        if (session->DecrementSessionRef() == 0)
            DisconnectSession(session);
    } else
        session->DecrementSessionRef();
}

// 사용자 요청 disconnect — 현재는 KillSession과 동일 동작 (wrapper)
// 향후 graceful disconnect (SendQ flush 후 close) 분기 시 본체 분리 예정
void NetClient::Disconnect(unsigned __int64 sessionID) {
    KillSession(sessionID);
}

void NetClient::DisconnectSession(Session* session) {
    LONG64 compareArr[2] = {FALSE, 0};
    if (InterlockedCompareExchange128(session->ReleaseArr, 0, TRUE, compareArr)) {
        OnDisconnect(session->SessionID);
        ReleaseSession(session);
    }
}


Packet* NetClient::AllocPacket(void) {
    TLSNodeMemoryPool<Packet>* packetPool = (TLSNodeMemoryPool<Packet>*)TlsGetValue(
        TLSChunkMemoryPool<Packet>::GetInstance()->GetTLSIndex());
    if (packetPool == NULL) {
        packetPool = new TLSNodeMemoryPool<Packet>;
        packetPool->SetTLS();
    }
    Packet* packet = packetPool->Alloc();
    packet->Clear();
    packet->IncrementRef();
    return packet;
}

void NetClient::FreePacket(Packet* packet) {
    if (packet->DecrementRef() != 0)
        return;
    TLSNodeMemoryPool<Packet>* packetPool = (TLSNodeMemoryPool<Packet>*)TlsGetValue(
        TLSChunkMemoryPool<Packet>::GetInstance()->GetTLSIndex());
    if (packetPool == NULL) {
        packetPool = new TLSNodeMemoryPool<Packet>;
        packetPool->SetTLS();
    }
    packetPool->Free(packet);
}


bool NetClient::CheckPacketMessageComplete(unsigned __int64 sessionID, Packet* recvQ) {
    std::size_t headerSize = Encoder->GetHeaderSize();
    DWORD packetUseSize = recvQ->GetDataSize();
    if (packetUseSize <= headerSize)
        return false;

    const void* headerBytes = recvQ->GetReadBufferPtr();
    if (!Encoder->VerifyHeaderMagic(headerBytes)) {
        KillSession(sessionID);
        return false;
    }

    std::size_t payloadLen;
    if (!Encoder->PeekPayloadLength(headerBytes, payloadLen)) {
        KillSession(sessionID);
        return false;
    }

    if (packetUseSize < headerSize + payloadLen)
        return false;

    return true;
}


bool NetClient::GetPacketMessage(Packet* packet, Packet* recvQ) {
    // Decode가 Front를 advance시키기 전에 페이로드 길이 미리 추출
    std::size_t payloadLen;
    if (!Encoder->PeekPayloadLength(recvQ->GetReadBufferPtr(), payloadLen))
        return false;

    if (!Encoder->Decode(*recvQ))
        return false;

    int retval_GetData = recvQ->GetData(packet->GetWriteBufferPtr(), (int)payloadLen);
    if (retval_GetData != (int)payloadLen) {
        Logger* pLogger = Logger::GetInstance();
        pLogger->Log(L"NetClient", Logger::LogLevel::kError,
                     L"# Packet GetData Func Err # EnqueueSize:%d, Return:%d", (int)payloadLen,
                     retval_GetData);
        pLogger->Crash();
    }
    packet->MoveWritePos((int)payloadLen);

    return true;
}

DWORD* NetClient::GetThreadTransmitArr(void) {
    // static thread_local: 스레드당 1개 캐시, 호출마다 lock 안 걸림
    // 다중 NetClient 인스턴스 사용 시 한계 있음 (1 client 가정)
    static thread_local DWORD* transmitArr = nullptr;
    if (transmitArr != nullptr)
        return transmitArr;

    transmitArr = new DWORD[4]();  // value-init: 0으로 초기화
    DWORD threadID = GetCurrentThreadId();

    AcquireSRWLockExclusive(&srwLogTransmitMap);
    LogTransmit_Map.insert(std::make_pair(threadID, transmitArr));
    ReleaseSRWLockExclusive(&srwLogTransmitMap);

    return transmitArr;
}

void NetClient::AddRecvBytes(DWORD recvBytes) {
    DWORD* arr = GetThreadTransmitArr();
    recvBytes += 40 * (recvBytes / 1460 + 1);
    InterlockedExchangeAdd(&arr[1], recvBytes);
}

void NetClient::AddRecvPacket(void) {
    DWORD* arr = GetThreadTransmitArr();
    InterlockedIncrement(&arr[0]);
}

void NetClient::AddSend(DWORD sendPacketCnt, DWORD sendBytes) {
    DWORD* arr = GetThreadTransmitArr();
    sendBytes += 40 * (sendBytes / 1460 + 1);
    InterlockedExchangeAdd(&arr[2], sendPacketCnt);
    InterlockedExchangeAdd(&arr[3], sendBytes);
}


void NetClient::GetTransmit(DWORD* transmitBuffer) {
    for (int i = 0; i < 4; i++)
        transmitBuffer[i] = 0;

    // 다른 스레드의 첫 호출(insert) 중 iteration race 방지
    AcquireSRWLockShared(&srwLogTransmitMap);
    for (auto& v : LogTransmit_Map) {
        for (int i = 0; i < 4; i++)
            transmitBuffer[i] += InterlockedExchange(&v.second[i], 0);
    }
    ReleaseSRWLockShared(&srwLogTransmitMap);
}
