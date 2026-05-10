#include "CNet_Server.h"


#include "MemoryPool_TLS.h"
#include "LockFree_Stack.h"
#include "Logger.h"
#include "XorPacketEncoder.h"

namespace {
constexpr BYTE kDefaultHeaderCode = 0x77;
constexpr BYTE kDefaultEncryptKey = 0x32;
}  // namespace

////////////////////////////////////////////////////////////////////////////////////////////////

unsigned WINAPI CNet_Server::AcceptThread(LPVOID lpThreadParameter) {
    Logger* pLogger = Logger::GetInstance();
    MemoryPool_TLS_Node<CPacket> packetPool;
    packetPool.SetTLS();

    CNet_Server* server = (CNet_Server*)lpThreadParameter;

    DWORD threadID = GetCurrentThreadId();

    SOCKET clientSocket;
    SOCKADDR_IN clientAddr;
    int addrSize = sizeof(clientAddr);

    while (1) {
        clientSocket = accept(server->ListenSocket, (SOCKADDR*)&clientAddr, &addrSize);
        if (clientSocket == INVALID_SOCKET) {
            int Err_Code = WSAGetLastError();
            switch (Err_Code) {
                case 10004:
                    break;
                default:
                    pLogger->Log(L"Accept", Logger::LogLevel::kSystem,
                                 L"AcceptFuc Err:%d\nMake Crash", Err_Code);
                    pLogger->Crash();
                    break;
            }
        }  // @@@@@ 단순 메모리 정리로 변경필요

        server->AddAcceptCnt();

        if (!server->OnConnectionRequest()) {
            closesocket(clientSocket);
            pLogger->Log(L"Accept", Logger::LogLevel::kDebug,
                         L"Max User Connected, ConnectedSession:%d\n",
                         server->GetSessionCnt_Connected());
            continue;
        }

        CNet_Server::stSESSION* session = server->GetFreeSession();
        if (session == NULL) {
            closesocket(clientSocket);
            pLogger->Log(L"Accept", Logger::LogLevel::kDebug,
                         L"Not Enough FreeSession, ConnectedSession:%d\n",
                         server->GetSessionCnt_Connected());
            continue;
        }

        session->Socket = clientSocket;
        /*InterlockedExchange(&session->Socket, clientSocket);*/
        session->ClientAddr = clientAddr;

        CreateIoCompletionPort((HANDLE)session->Socket, server->IOCP, (ULONG_PTR)session, 0);

        server->OnClientJoin(session->SessionID);

        server->RecvPost(session);


        if (session->DecrementSessionRef() == 0)
            server->DisconnectSession(session);
    }

    return 0;
}

unsigned WINAPI CNet_Server::WorkerThread(LPVOID lpThreadParameter) {
    Logger* pLogger = Logger::GetInstance();
    MemoryPool_TLS_Node<CPacket> packetPool;
    packetPool.SetTLS();

    CNet_Server* server = (CNet_Server*)lpThreadParameter;

    DWORD threadID = GetCurrentThreadId();

    DWORD transferred;
    CNet_Server::stSESSION* targetSession;
    OVERLAPPED* tmpOverlapped;

    while (1) {
        GetQueuedCompletionStatus(server->IOCP, &transferred, (PULONG_PTR)&targetSession,
                                  &tmpOverlapped, INFINITE);

        if (transferred == 0 && targetSession == NULL && tmpOverlapped == NULL) {
            pLogger->Log(L"Network", Logger::LogLevel::kSystem, L"# GQCS return NULL");
            pLogger->Crash();
            break;
        }

        if (transferred != 0) {
            // Recv 완료통지
            if (&targetSession->RecvOverlapped == tmpOverlapped) {
                /*server->AddRecv(transferred);*/
                server->AddRecvBytes(transferred);
                targetSession->RecvQ.MoveWritePos(transferred);


                while (1) {
                    if (!server->CheckPacketMessageComplete(targetSession->SessionID,
                                                             &targetSession->RecvQ))
                        break;

                    CPacket* packet = server->AllocPacket();
                    if (server->GetPacketMessage(packet, &targetSession->RecvQ) ==
                        false)  // Decode Fail, Need to Disconnect
                    {
                        server->FreePacket(packet);
                        server->KillSession(targetSession->SessionID);

                        WCHAR ipBuf[32];
                        pLogger->Log(
                            L"NetServer", Logger::LogLevel::kSystem,
                            L"Packet Decode Failed, sessionID:%p, IP:%s", targetSession->SessionID,
                            InetNtop(AF_INET, &targetSession->ClientAddr.sin_addr, ipBuf, 32));
                        break;
                    }
                    server->AddRecvPacket();
                    server->OnRecv(targetSession->SessionID, packet);

                    server->FreePacket(packet);
                }
                int remainSize = targetSession->RecvQ.GetDataSize();
                if (remainSize)
                    targetSession->RecvQ.ShiftDataToFront();
                else
                    targetSession->RecvQ.Clear();

                server->RecvPost(targetSession);
            }

            // Send 완료통지
            else if (&targetSession->SendOverlapped == tmpOverlapped) {
                server->AddSend(targetSession->SendPacketCnt, transferred);

                int tmpPacketCnt = targetSession->SendPacketCnt;
                for (int i = 0; i < tmpPacketCnt; i++) {
                    CPacket* packet = targetSession->SendBuffer[i];
                    server->FreePacket(packet);
                }
                targetSession->SendPacketCnt = 0;

                targetSession->SendFlag = 0;

                targetSession->IncrementSessionRef();
                if (!server->SendPost(targetSession))
                    targetSession->DecrementSessionRef();
            }

            // SendPacket
            else if (tmpOverlapped == (LPOVERLAPPED)0xfffffffffffffffe) {
                targetSession->IncrementSessionRef();
                if (!server->SendPost(targetSession))
                    targetSession->DecrementSessionRef();
            }


            else {
                pLogger->Log(L"NetServer", Logger::LogLevel::kError,
                             L"Overlapped Pointer Error, sessionID:%p, UseFlag:%d, DeleteFlag:%d, "
                             L"SessionRef:%d",
                             targetSession->SessionID, targetSession->SessionUseFlag,
                             targetSession->ReleaseArr[0], targetSession->ReleaseArr[1]);
                pLogger->Crash();
            }
        }

        if (targetSession->DecrementSessionRef() == 0)
            server->DisconnectSession(targetSession);
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

CNet_Server::CNet_Server(IPacketEncoder* encoder) : Encoder(encoder), OwnsEncoder(false) {
    if (Encoder == nullptr) {
        Encoder = new XorPacketEncoder(kDefaultHeaderCode, kDefaultEncryptKey);
        OwnsEncoder = true;
    }

    IOCP = NULL;
    ListenSocket = INVALID_SOCKET;

    SessionArr = NULL;
    SessionCnt_Total = 0;

    SessionID_Cnt = 0;

    AcceptThreadID = 0;
    AcceptThread_ = NULL;

    WorkerThreadID = NULL;
    WorkerThread_ = NULL;
    ThreadCnt = 0;

    AcceptTotal = 0;
    AcceptTPS = 0;

    PacketPool = MemoryPool_TLS_Chunck<CPacket>::GetInstance();

    InitializeSRWLock(&srwLogTransmitMap);
}

CNet_Server::~CNet_Server() {
    for (int i = 0; i < ThreadCnt; i++)
        CloseHandle(WorkerThread_[i]);
    delete[] WorkerThread_;
    delete[] WorkerThreadID;

    if (SessionCnt_Total)
        delete[] SessionArr;

    CloseHandle(AcceptThread_);

    closesocket(ListenSocket);

    WSACleanup();

    if (OwnsEncoder) {
        delete Encoder;
        Encoder = nullptr;
    }
}

bool CNet_Server::Start(const WCHAR* serverIP, u_short serverPort, u_short workerThreadCnt_Total,
                        u_short workerThreadCnt_Run, BOOL nagle, u_short connectSession_Max) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, workerThreadCnt_Run);
    if (IOCP == NULL)
        return false;

    ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ListenSocket == INVALID_SOCKET)
        return false;

    LINGER linger;
    linger.l_onoff = 1;
    linger.l_linger = 0;
    int retval_setsockopt =
        setsockopt(ListenSocket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
    if (retval_setsockopt == SOCKET_ERROR)
        return false;

    /*int sndbuf = 0;
	retval_setsockopt = setsockopt(ListenSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));
	if (retval_setsockopt == SOCKET_ERROR) return false;*/

    if (nagle) {
        BOOL nagleValue = TRUE;
        retval_setsockopt = setsockopt(ListenSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nagleValue,
                                       sizeof(nagleValue));
        if (retval_setsockopt == SOCKET_ERROR)
            return false;
    }

    SOCKADDR_IN serveraddr;
    serveraddr.sin_family = AF_INET;
    if (serverIP == NULL)
        serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
        InetPton(AF_INET, serverIP, &serveraddr.sin_addr);
    serveraddr.sin_port = htons(serverPort);

    int retval_bind = bind(ListenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
    if (retval_bind == SOCKET_ERROR)
        return false;

    int retval_listen = listen(ListenSocket, SOMAXCONN_HINT(60000));
    if (retval_listen == SOCKET_ERROR)
        return false;

    wprintf(L"[%s:%d] Listening\n", serverIP, serverPort);

    //////////////////////////////////////////

    SessionCnt_Total = connectSession_Max;
    SessionArr = new stSESSION[SessionCnt_Total];
    for (u_short init_SessionArr = 0; init_SessionArr < SessionCnt_Total; init_SessionArr++) {
        SessionArr[init_SessionArr].ReleaseArr[0] = TRUE;
        SessionArr[init_SessionArr].ReleaseArr[1] = 0;
        SessionArr[init_SessionArr].SessionID = (unsigned __int64)init_SessionArr << 48;

        FreeSessionStack.Push(&SessionArr[init_SessionArr]);
    }

    AcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, (LPVOID)this, 0,
                                           (unsigned int*)&AcceptThreadID);
    if (AcceptThread_ == NULL)
        return false;

    ThreadCnt = workerThreadCnt_Total;
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

void CNet_Server::SendPacket(unsigned __int64 sessionID, CPacket* packet) {
    stSESSION* session = FindSession(sessionID);

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

bool CNet_Server::SendPost(stSESSION* session, DWORD flag) {
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
    CPacket* tmpPacket;
    while (packetCnt--) {
        if (session->SendQ.Dequeue(tmpPacket) == false) {
            Logger* pLogger = Logger::GetInstance();
            pLogger->Log(
                L"NetServer", Logger::LogLevel::kError,
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
                pLogger->Log(L"NetServer", Logger::LogLevel::kError,
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


bool CNet_Server::RecvPost(stSESSION* session, DWORD flag) {
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
                pLogger->Log(L"NetServer", Logger::LogLevel::kError,
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


CNet_Server::stSESSION* CNet_Server::GetFreeSession() {
    stSESSION* session;
    FreeSessionStack.Pop(session);
    if (session == NULL)
        return NULL;

    initSession(session);

    return session;
}

void CNet_Server::ReleaseSession(stSESSION* session) {
    session->SessionID &= 0xffff000000000000;
    closesocket(session->Socket);
    /*InterlockedExchange(&session->Socket, INVALID_SOCKET);*/

    int remainSize = session->SendQ.GetUseSize();
    CPacket* packet;
    while (session->SendQ.Dequeue(packet))
        FreePacket(packet);

    remainSize = session->SendPacketCnt;
    for (int i = 0; i < remainSize; i++)
        FreePacket(session->SendBuffer[i]);
    session->SendPacketCnt = 0;

    FreeSessionStack.Push(session);
}

void CNet_Server::initSession(stSESSION* session) {
    session->SessionUseFlag = true;
    session->SessionID |= GetSessionID_New();

    session->LastAction = NULL;

    ZeroMemory(&session->SendOverlapped, sizeof(session->SendOverlapped));
    ZeroMemory(&session->RecvOverlapped, sizeof(session->RecvOverlapped));

    session->SendFlag = 0;
    session->SendPacketCnt = 0;

    session->RecvQ.Clear();
    session->SendQ.Clear();

    session->IncrementSessionRef();
    session->ReleaseArr[0] = FALSE;
}

void CNet_Server::KillSession(unsigned __int64 sessionID) {
    stSESSION* session = FindSession(sessionID);
    session->IncrementSessionRef();
    if (session->ReleaseArr[0] == FALSE && session->SessionID == sessionID) {
        session->SessionUseFlag = false;
        CancelIoEx((HANDLE)session->Socket, NULL);
        if (session->DecrementSessionRef() == 0)
            DisconnectSession(session);
    } else
        session->DecrementSessionRef();
}

void CNet_Server::DisconnectSession(stSESSION* session) {
    LONG64 compareArr[2] = {FALSE, 0};
    if (InterlockedCompareExchange128(session->ReleaseArr, 0, TRUE, compareArr)) {
        OnClientLeave(session->SessionID);
        ReleaseSession(session);
    }
}


CPacket* CNet_Server::AllocPacket(void) {
    MemoryPool_TLS_Node<CPacket>* packetPool = (MemoryPool_TLS_Node<CPacket>*)TlsGetValue(
        MemoryPool_TLS_Chunck<CPacket>::GetInstance()->GetTLSIndex());
    if (packetPool == NULL) {
        packetPool = new MemoryPool_TLS_Node<CPacket>;
        packetPool->SetTLS();
    }
    CPacket* packet = packetPool->Alloc();
    packet->Clear();
    packet->IncrementRef();
    return packet;
}

void CNet_Server::FreePacket(CPacket* packet) {
    if (packet->DecrementRef() != 0)
        return;
    MemoryPool_TLS_Node<CPacket>* packetPool = (MemoryPool_TLS_Node<CPacket>*)TlsGetValue(
        MemoryPool_TLS_Chunck<CPacket>::GetInstance()->GetTLSIndex());
    if (packetPool == NULL) {
        packetPool = new MemoryPool_TLS_Node<CPacket>;
        packetPool->SetTLS();
    }
    packetPool->Free(packet);
}


bool CNet_Server::CheckPacketMessageComplete(unsigned __int64 sessionID, CPacket* recvQ) {
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


bool CNet_Server::GetPacketMessage(CPacket* packet, CPacket* recvQ) {
    // Decode가 Front를 advance시키기 전에 페이로드 길이 미리 추출
    std::size_t payloadLen;
    if (!Encoder->PeekPayloadLength(recvQ->GetReadBufferPtr(), payloadLen))
        return false;

    if (!Encoder->Decode(*recvQ))
        return false;

    int retval_GetData = recvQ->GetData(packet->GetWriteBufferPtr(), (int)payloadLen);
    if (retval_GetData != (int)payloadLen) {
        Logger* pLogger = Logger::GetInstance();
        pLogger->Log(L"NetServer", Logger::LogLevel::kError,
                     L"# CPacket GetData Func Err # EnqueueSize:%d, Return:%d", (int)payloadLen,
                     retval_GetData);
        pLogger->Crash();
    }
    packet->MoveWritePos((int)payloadLen);

    return true;
}

DWORD* CNet_Server::GetThreadTransmitArr(void) {
    // static thread_local: 스레드당 1개 캐시, 호출마다 lock 안 걸림
    // 다중 CNet_Server 인스턴스 사용 시 한계 있음 (1 서버 가정)
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

void CNet_Server::AddRecvBytes(DWORD recvBytes) {
    DWORD* arr = GetThreadTransmitArr();
    recvBytes += 40 * (recvBytes / 1460 + 1);
    InterlockedExchangeAdd(&arr[1], recvBytes);
}

void CNet_Server::AddRecvPacket(void) {
    DWORD* arr = GetThreadTransmitArr();
    InterlockedIncrement(&arr[0]);
}

void CNet_Server::AddSend(DWORD sendPacketCnt, DWORD sendBytes) {
    DWORD* arr = GetThreadTransmitArr();
    sendBytes += 40 * (sendBytes / 1460 + 1);
    InterlockedExchangeAdd(&arr[2], sendPacketCnt);
    InterlockedExchangeAdd(&arr[3], sendBytes);
}


void CNet_Server::GetTransmit(DWORD* transmitBuffer) {
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