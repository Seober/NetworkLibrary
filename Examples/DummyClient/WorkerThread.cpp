// FD_SETSIZE 오버라이드 — 기본 64에서 1024로 확장
// (winsock2.h가 이를 보고 fd_set 크기 결정)
#define FD_SETSIZE 1024

#include "WorkerThread.h"
#include "../ChatServer/ChatProtocol.h"
#include "CPacket.h"

#include <process.h>
#include <stdio.h>
#include <string.h>

#define HEARTBEAT_INTERVAL_MS 30000
#define SELECT_TIMEOUT_MS 50

// 재연결 대기 범위 (0.5~2초) — 서버에 burst reconnect 부담 분산
#define MIN_RECONNECT_DELAY_MS 500
#define MAX_RECONNECT_DELAY_MS 2000

static DWORD ComputeDisconnectAt(const WorkerContext* ctx) {
    if (ctx->minLifetimeMs == 0)
        return 0;  // 무작위 disconnect 비활성 — disconnectAt=0 sentinel
    return GetTickCount() + ctx->minLifetimeMs +
           (rand() % (ctx->maxLifetimeMs - ctx->minLifetimeMs));
}

static DWORD ComputeReconnectAt(void) {
    return GetTickCount() + MIN_RECONNECT_DELAY_MS +
           (rand() % (MAX_RECONNECT_DELAY_MS - MIN_RECONNECT_DELAY_MS));
}

static void ResetSessionForReconnect(ClientSession& s) {
    s.sendBytes = 0;
    s.recvBytes = 0;
    s.lastHeartbeat = 0;
    s.lastMessage = 0;
    s.disconnectAt = 0;
}

// 비동기 connect 시작 (재연결 진입로). 성공 시 CONNECTED, 진행 중이면 CONNECTING, 실패 시 DISCONNECTED + reconnectAt 갱신
static void InitiateConnect(ClientSession& s, WorkerContext* ctx) {
    s.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.sock == INVALID_SOCKET) {
        s.reconnectAt = ComputeReconnectAt();
        s.state = SessionState::kDisconnected;
        return;
    }

    BOOL nodelay = TRUE;
    setsockopt(s.sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    u_long mode = 1;
    ioctlsocket(s.sock, FIONBIO, &mode);

    int rc = connect(s.sock, (SOCKADDR*)&ctx->serverAddr, sizeof(SOCKADDR_IN));
    if (rc == 0) {
        // 즉시 완료 (드물지만 가능)
        ResetSessionForReconnect(s);
        s.state = SessionState::kConnected;
        s.lastHeartbeat = GetTickCount();
        s.lastMessage = GetTickCount();
        InterlockedIncrement(ctx->Connected);
    } else {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            s.state = SessionState::kConnecting;
        } else {
            closesocket(s.sock);
            s.sock = INVALID_SOCKET;
            s.state = SessionState::kDisconnected;
            s.reconnectAt = ComputeReconnectAt();
        }
    }
}

// ACTIVE → DISCONNECTED 전환 시 Active 감소 (현재 active 수 추적용)
static void MarkDisconnected(ClientSession& s, WorkerContext* ctx) {
    if (s.state == SessionState::kActive)
        InterlockedDecrement(ctx->Active);
    s.state = SessionState::kDisconnected;
}

// CONNECTING 세션의 select 결과 처리
static void HandleConnectComplete(ClientSession& s, WorkerContext* ctx, bool writable, bool error) {
    if (error) {
        closesocket(s.sock);
        s.sock = INVALID_SOCKET;
        s.state = SessionState::kDisconnected;
        s.reconnectAt = ComputeReconnectAt();
        return;
    }
    if (writable) {
        int sockErr = 0;
        int len = sizeof(sockErr);
        getsockopt(s.sock, SOL_SOCKET, SO_ERROR, (char*)&sockErr, &len);
        if (sockErr == 0) {
            ResetSessionForReconnect(s);
            s.state = SessionState::kConnected;
            s.lastHeartbeat = GetTickCount();
            s.lastMessage = GetTickCount();
            InterlockedIncrement(ctx->Connected);
        } else {
            closesocket(s.sock);
            s.sock = INVALID_SOCKET;
            s.state = SessionState::kDisconnected;
            s.reconnectAt = ComputeReconnectAt();
        }
    }
}

static void SendLoginPacket(ClientSession& s, WorkerContext* ctx, int globalIdx) {
    CPacket packet;
    packet.Clear();

    WORD type = kCsChatReqLogin;
    __int64 accountNo = (__int64)globalIdx + 1;

    WCHAR id[20] = {0};
    WCHAR nickname[20] = {0};
    swprintf_s(id, 20, L"DC%lld", accountNo);
    swprintf_s(nickname, 20, L"DC%lld", accountNo);

    char sessionKey[64] = {0};

    packet << type;
    packet << accountNo;
    packet.PutData(id, 20);
    packet.PutData(nickname, 20);
    packet.PutData(sessionKey, 64);

    ctx->encoder->Encode(packet);

    int totalLen = packet.GetDataSize();
    int copyLen = totalLen;
    if (s.sendBytes + copyLen > sizeof(s.sendBuf))
        return;

    memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
    s.sendBytes += copyLen;

    s.accountNo = accountNo;
    s.state = SessionState::kLoginSent;
}

static void SendSectorMovePacket(ClientSession& s, WorkerContext* ctx) {
    CPacket packet;
    packet.Clear();

    WORD type = kCsChatReqSectorMove;
    WORD sectorX = (WORD)(rand() % 50);
    WORD sectorY = (WORD)(rand() % 50);

    packet << type;
    packet << s.accountNo;
    packet << sectorX;
    packet << sectorY;

    ctx->encoder->Encode(packet);

    int copyLen = packet.GetDataSize();
    if (s.sendBytes + copyLen > sizeof(s.sendBuf))
        return;

    memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
    s.sendBytes += copyLen;

    s.sectorX = sectorX;
    s.sectorY = sectorY;
}

static void SendChatPacket(ClientSession& s, WorkerContext* ctx) {
    CPacket packet;
    packet.Clear();

    WORD type = kCsChatReqMessage;
    WCHAR message[64];
    int len = swprintf_s(message, 64, L"Test from DC%lld", s.accountNo);
    WORD messageLen = (WORD)(len * sizeof(WCHAR));

    packet << type;
    packet << s.accountNo;
    packet << messageLen;
    packet.PutData(message, len);

    ctx->encoder->Encode(packet);

    int copyLen = packet.GetDataSize();
    if (s.sendBytes + copyLen > sizeof(s.sendBuf))
        return;

    memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
    s.sendBytes += copyLen;

    s.messagesSent++;
}

static void SendHeartbeatPacket(ClientSession& s, WorkerContext* ctx) {
    CPacket packet;
    packet.Clear();

    WORD type = kCsChatReqHeartbeat;
    packet << type;

    ctx->encoder->Encode(packet);

    int copyLen = packet.GetDataSize();
    if (s.sendBytes + copyLen > sizeof(s.sendBuf))
        return;

    memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
    s.sendBytes += copyLen;
}

// recvBuf에 누적된 데이터에서 완성된 패킷을 모두 처리
// 반환: 처리된 바이트 수
static int ProcessRecvBuffer(ClientSession& s, WorkerContext* ctx) {
    int processed = 0;
    std::size_t headerSize = ctx->encoder->GetHeaderSize();

    while (s.recvBytes - processed >= (int)headerSize) {
        const void* hdr = s.recvBuf + processed;

        if (!ctx->encoder->VerifyHeaderMagic(hdr)) {
            // 잘못된 패킷 — 세션 종료
            MarkDisconnected(s, ctx);
            return processed;
        }

        std::size_t payloadLen;
        if (!ctx->encoder->PeekPayloadLength(hdr, payloadLen)) {
            MarkDisconnected(s, ctx);
            return processed;
        }

        if (payloadLen > sizeof(s.recvBuf) - headerSize) {
            MarkDisconnected(s, ctx);
            return processed;
        }

        int packetSize = (int)(headerSize + payloadLen);
        if (s.recvBytes - processed < packetSize)
            break;  // 페이로드 부족

        // 임시 CPacket으로 복호화
        CPacket tmpPacket;
        memcpy(tmpPacket.GetWriteBufferPtr(), s.recvBuf + processed, packetSize);
        tmpPacket.MoveWritePos(packetSize);

        if (!ctx->encoder->Decode(tmpPacket)) {
            // checksum 실패 — 세션 종료
            MarkDisconnected(s, ctx);
            return processed;
        }

        // 패킷 타입 분기
        WORD type;
        tmpPacket >> type;

        switch (type) {
            case kCsChatResLogin: {
                BYTE status;
                __int64 accountNo;
                tmpPacket >> status >> accountNo;
                if (status == 1) {
                    s.state = SessionState::kActive;
                    s.disconnectAt = ComputeDisconnectAt(ctx);
                    InterlockedIncrement(ctx->Active);
                    // 로그인 성공 즉시 섹터 이동
                    SendSectorMovePacket(s, ctx);
                } else {
                    // 로그인 실패 — disconnect 후 재연결 사이클로
                    MarkDisconnected(s, ctx);
                    s.reconnectAt = ComputeReconnectAt();
                    InterlockedIncrement(ctx->Failed);
                }
                break;
            }
            case kCsChatResMessage: {
                s.messagesRecv++;
                InterlockedIncrement(ctx->TPS_Recv);
                InterlockedIncrement64(ctx->TotalRecv);
                break;
            }
            case kCsChatResSectorMove:
                break;
            default:
                break;
        }

        processed += packetSize;
    }

    return processed;
}

static void HandleRecv(ClientSession& s, WorkerContext* ctx) {
    int freeSpace = sizeof(s.recvBuf) - s.recvBytes;
    if (freeSpace <= 0) {
        // 버퍼 가득 — 비정상
        MarkDisconnected(s, ctx);
        return;
    }

    int n = recv(s.sock, s.recvBuf + s.recvBytes, freeSpace, 0);
    if (n == 0 || (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
        MarkDisconnected(s, ctx);
        return;
    }
    if (n == SOCKET_ERROR)
        return;  // WSAEWOULDBLOCK

    s.recvBytes += n;

    int processed = ProcessRecvBuffer(s, ctx);
    if (processed > 0) {
        if (processed < s.recvBytes)
            memmove(s.recvBuf, s.recvBuf + processed, s.recvBytes - processed);
        s.recvBytes -= processed;
    }
}

static void HandleSend(ClientSession& s, WorkerContext* ctx) {
    if (s.sendBytes == 0)
        return;

    int n = send(s.sock, s.sendBuf, s.sendBytes, 0);
    if (n == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            return;
        MarkDisconnected(s, ctx);
        return;
    }

    if (n < s.sendBytes)
        memmove(s.sendBuf, s.sendBuf + n, s.sendBytes - n);
    s.sendBytes -= n;

    InterlockedExchangeAdd(ctx->TPS_Sent, n);
    InterlockedExchangeAdd64(ctx->TotalSent, n);
}

unsigned __stdcall WorkerThreadFunc(LPVOID param) {
    WorkerContext* ctx = (WorkerContext*)param;

    // 스레드별 rand 시드 (재연결 random 분산용)
    srand(GetTickCount() ^ GetCurrentThreadId());

    // Phase 1: 모든 세션 초기 connect (blocking, 순차). 실패 시 DISCONNECTED + reconnectAt
    for (int i = 0; i < ctx->sessionCount; i++) {
        ClientSession& s = ctx->sessions[i];
        s.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s.sock == INVALID_SOCKET) {
            s.state = SessionState::kDisconnected;
            s.reconnectAt = ComputeReconnectAt();
            InterlockedIncrement(ctx->Failed);
            continue;
        }

        BOOL nodelay = TRUE;
        setsockopt(s.sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

        if (connect(s.sock, (SOCKADDR*)&ctx->serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
            closesocket(s.sock);
            s.sock = INVALID_SOCKET;
            s.state = SessionState::kDisconnected;
            s.reconnectAt = ComputeReconnectAt();
            InterlockedIncrement(ctx->Failed);
            continue;
        }

        u_long mode = 1;
        ioctlsocket(s.sock, FIONBIO, &mode);

        s.state = SessionState::kConnected;
        s.lastHeartbeat = GetTickCount();
        s.lastMessage = GetTickCount();
        InterlockedIncrement(ctx->Connected);
    }

    // Phase 2: select 루프 — 무작위 disconnect + 자동 재연결
    while (WaitForSingleObject(ctx->shutdownEvent, 0) == WAIT_TIMEOUT) {
        fd_set readfds, writefds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        int activeSocketCnt = 0;
        for (int i = 0; i < ctx->sessionCount; i++) {
            ClientSession& s = ctx->sessions[i];
            if (s.sock == INVALID_SOCKET)
                continue;
            if (s.state >= SessionState::kDisconnected)
                continue;

            if (s.state == SessionState::kConnecting) {
                // non-blocking connect 완료/실패 감지
                FD_SET(s.sock, &writefds);
                FD_SET(s.sock, &exceptfds);
            } else {
                FD_SET(s.sock, &readfds);
                if (s.sendBytes > 0)
                    FD_SET(s.sock, &writefds);
            }
            activeSocketCnt++;
        }

        if (activeSocketCnt == 0) {
            Sleep(SELECT_TIMEOUT_MS);
        } else {
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = SELECT_TIMEOUT_MS * 1000;

            int selectResult = select(0, &readfds, &writefds, &exceptfds, &tv);

            if (selectResult > 0) {
                for (int i = 0; i < ctx->sessionCount; i++) {
                    ClientSession& s = ctx->sessions[i];
                    if (s.sock == INVALID_SOCKET)
                        continue;

                    if (s.state == SessionState::kConnecting) {
                        bool writable = FD_ISSET(s.sock, &writefds) != 0;
                        bool errored = FD_ISSET(s.sock, &exceptfds) != 0;
                        if (writable || errored)
                            HandleConnectComplete(s, ctx, writable, errored);
                    } else {
                        if (FD_ISSET(s.sock, &readfds))
                            HandleRecv(s, ctx);
                        if (s.state < SessionState::kDisconnected && FD_ISSET(s.sock, &writefds))
                            HandleSend(s, ctx);
                    }
                }
            }
        }

        // 주기 동작 (로그인/메시지/하트비트/disconnect 타이머/재연결)
        DWORD now = GetTickCount();
        for (int i = 0; i < ctx->sessionCount; i++) {
            ClientSession& s = ctx->sessions[i];

            if (s.state == SessionState::kConnected) {
                int globalIdx = ctx->sessionStartIdx + i;
                SendLoginPacket(s, ctx, globalIdx);
            } else if (s.state == SessionState::kActive) {
                // 무작위 disconnect 시점 도달 — 끊김 (disconnectAt=0이면 비활성)
                if (s.disconnectAt != 0 && now >= s.disconnectAt) {
                    MarkDisconnected(s, ctx);
                } else {
                    if (now - s.lastMessage >= ctx->messageInterval) {
                        SendChatPacket(s, ctx);
                        s.lastMessage = now;
                    }
                    if (now - s.lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
                        SendHeartbeatPacket(s, ctx);
                        s.lastHeartbeat = now;
                    }
                }
            } else if (s.state == SessionState::kDisconnected) {
                if (s.sock != INVALID_SOCKET) {
                    closesocket(s.sock);
                    s.sock = INVALID_SOCKET;
                    InterlockedIncrement(ctx->Disconnected);
                    s.reconnectAt = ComputeReconnectAt();  // 매 cycle 새 delay
                } else if (now >= s.reconnectAt) {
                    InitiateConnect(s, ctx);
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < ctx->sessionCount; i++) {
        if (ctx->sessions[i].sock != INVALID_SOCKET) {
            closesocket(ctx->sessions[i].sock);
            ctx->sessions[i].sock = INVALID_SOCKET;
        }
    }

    return 0;
}
