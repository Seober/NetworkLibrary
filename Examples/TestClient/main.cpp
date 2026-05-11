#pragma comment(lib, "ws2_32")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include <atomic>

#include "NetClient.h"
#include "Packet.h"
#include "../TestServer/TestProtocol.h"


#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 12001

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// NetServer 부하 테스트 클라이언트 — N개 동시 접속, random interval 송신,
// echo 응답 검증 + 메트릭 출력. Mode 1=연결유지 / Mode 2=lifetime 만료 후 reconnect.
class TestClient : public NetClient {
public:
    struct ClientState {
        std::atomic<unsigned __int64> currentSessionID{0};
        std::atomic<bool> disconnected{false};
        // 아래 3 필드는 sender thread만 write
        DWORD nextSendTime = 0;
        DWORD lifetimeEnd = 0;
        DWORD reconnectAt = 0;
    };

    void OnConnect(unsigned __int64 sessionID) override {
        ConnectCnt++;
    }

    void OnDisconnect(unsigned __int64 sessionID) override {
        DisconnectCnt++;
        // O(N) lookup — N < 10000에서 OK. 향후 map<sessionID, idx> 옵션.
        for (int i = 0; i < N; i++) {
            if (States[i].currentSessionID.load() == sessionID) {
                States[i].disconnected.store(true);
                break;
            }
        }
    }

    void OnRecv(unsigned __int64 sessionID, Packet* packet) override {
        RecvCnt++;
        WORD type;
        *packet >> type;
        if (type == kTestEcho)
            VerifiedCnt++;
    }

    int N = 0;
    ClientState* States = nullptr;
    std::atomic<int> ConnectCnt{0};
    std::atomic<int> DisconnectCnt{0};
    std::atomic<int> RecvCnt{0};
    std::atomic<int> VerifiedCnt{0};
};

struct SenderContext {
    TestClient* client;
    char serverIP[64];
    int serverPort;
    int mode;   // 1=Keep-Alive, 2=Reconnect
    DWORD intervalMin;
    DWORD intervalMax;
    DWORD lifetimeMin;
    DWORD lifetimeMax;
    DWORD reconnectMin;
    DWORD reconnectMax;
    HANDLE shutdownEvent;
};

inline DWORD RandomBetween(DWORD min, DWORD max) {
    if (max <= min)
        return min;
    return min + (rand() % (max - min + 1));
}

unsigned WINAPI SenderThread(LPVOID param) {
    SenderContext* ctx = (SenderContext*)param;
    TestClient* client = ctx->client;

    WCHAR serverIPWide[64];
    MultiByteToWideChar(CP_ACP, 0, ctx->serverIP, -1, serverIPWide, 64);

    while (WaitForSingleObject(ctx->shutdownEvent, 0) != WAIT_OBJECT_0) {
        DWORD curTick = GetTickCount();

        for (int i = 0; i < client->N; i++) {
            TestClient::ClientState* s = &client->States[i];
            unsigned __int64 sid = s->currentSessionID.load();
            bool isDisconnected = s->disconnected.load();

            if (sid == 0 || isDisconnected) {
                // disconnected — Mode 2이면 reconnect 시도
                if (ctx->mode == 2 && curTick >= s->reconnectAt) {
                    unsigned __int64 newSid = client->Connect(serverIPWide, (u_short)ctx->serverPort);
                    if (newSid != 0) {
                        s->currentSessionID.store(newSid);
                        s->disconnected.store(false);
                        s->nextSendTime =
                            curTick + RandomBetween(ctx->intervalMin, ctx->intervalMax);
                        s->lifetimeEnd =
                            curTick + RandomBetween(ctx->lifetimeMin, ctx->lifetimeMax);
                    }
                }
                continue;
            }

            // connected
            if (ctx->mode == 2 && curTick >= s->lifetimeEnd) {
                client->Disconnect(sid);
                s->reconnectAt = curTick + RandomBetween(ctx->reconnectMin, ctx->reconnectMax);
                continue;
            }

            if (curTick >= s->nextSendTime) {
                Packet* p = client->AllocPacket();
                WORD type = kTestEcho;
                int payload = rand();
                *p << type << payload;
                client->SendPacket(sid, p);
                client->FreePacket(p);
                s->nextSendTime = curTick + RandomBetween(ctx->intervalMin, ctx->intervalMax);
            }
        }
        Sleep(10);
    }
    return 0;
}

}  // namespace


int wmain(int argc, WCHAR* argv[]) {
    // 명령행: IP, Port (디폴트 fallback)
    char serverIp[64] = DEFAULT_IP;
    int serverPort = DEFAULT_PORT;
    if (argc >= 2)
        WideCharToMultiByte(CP_ACP, 0, argv[1], -1, serverIp, sizeof(serverIp), NULL, NULL);
    if (argc >= 3)
        serverPort = _wtoi(argv[2]);

    wprintf(L"=== TestClient ===\nServer: %hs:%d\n\n", serverIp, serverPort);

    // 콘솔 입력
    int N = 0;
    DWORD intervalMin = 0;
    DWORD intervalMax = 0;
    int mode = 1;
    DWORD lifetimeMin = 0;
    DWORD lifetimeMax = 0;
    DWORD reconnectMin = 0;
    DWORD reconnectMax = 0;

    wprintf(L"Input Client Count (N): ");
    wscanf_s(L"%d", &N);
    wprintf(L"Input Send Interval Min (ms): ");
    wscanf_s(L"%u", &intervalMin);
    wprintf(L"Input Send Interval Max (ms): ");
    wscanf_s(L"%u", &intervalMax);
    wprintf(L"Mode (1: Keep-Alive, 2: Reconnect): ");
    wscanf_s(L"%d", &mode);
    if (mode == 2) {
        wprintf(L"Input Lifetime Min (ms): ");
        wscanf_s(L"%u", &lifetimeMin);
        wprintf(L"Input Lifetime Max (ms): ");
        wscanf_s(L"%u", &lifetimeMax);
        wprintf(L"Input Reconnect Delay Min (ms): ");
        wscanf_s(L"%u", &reconnectMin);
        wprintf(L"Input Reconnect Delay Max (ms): ");
        wscanf_s(L"%u", &reconnectMax);
    }

    if (N <= 0 || intervalMin == 0 || intervalMax < intervalMin) {
        wprintf(L"Invalid input\n");
        return 1;
    }
    if (mode == 2 && (lifetimeMin == 0 || lifetimeMax < lifetimeMin || reconnectMax < reconnectMin)) {
        wprintf(L"Invalid Mode 2 input\n");
        return 1;
    }

    srand((unsigned)GetTickCount());

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int processorCnt = (int)sysInfo.dwNumberOfProcessors - 1;
    if (processorCnt < 1)
        processorCnt = 1;

    TestClient client;
    if (!client.Start((u_short)processorCnt, 0, (u_short)N)) {
        wprintf(L"NetClient Start failed\n");
        return 1;
    }

    client.N = N;
    client.States = new TestClient::ClientState[N];
    DWORD startTick = GetTickCount();
    for (int i = 0; i < N; i++) {
        client.States[i].nextSendTime = startTick;
        client.States[i].lifetimeEnd =
            (mode == 2) ? startTick + RandomBetween(lifetimeMin, lifetimeMax) : 0;
        client.States[i].reconnectAt = startTick;
    }

    WCHAR serverIpWide[64];
    MultiByteToWideChar(CP_ACP, 0, serverIp, -1, serverIpWide, 64);

    // 초기 N개 connect (main thread)
    wprintf(L"\nConnecting %d clients...\n", N);
    for (int i = 0; i < N; i++) {
        unsigned __int64 sid = client.Connect(serverIpWide, (u_short)serverPort);
        if (sid == 0) {
            wprintf(L"Initial Connect failed at idx %d\n", i);
            break;
        }
        client.States[i].currentSessionID.store(sid);
    }

    // Sender thread 시작
    HANDLE shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    SenderContext ctx;
    ctx.client = &client;
    strcpy_s(ctx.serverIP, serverIp);
    ctx.serverPort = serverPort;
    ctx.mode = mode;
    ctx.intervalMin = intervalMin;
    ctx.intervalMax = intervalMax;
    ctx.lifetimeMin = lifetimeMin;
    ctx.lifetimeMax = lifetimeMax;
    ctx.reconnectMin = reconnectMin;
    ctx.reconnectMax = reconnectMax;
    ctx.shutdownEvent = shutdownEvent;

    HANDLE senderHandle = (HANDLE)_beginthreadex(NULL, 0, SenderThread, &ctx, 0, NULL);

    wprintf(L"\nPress Q to quit\n\n");

    // 메인 루프 — 1초마다 메트릭 출력
    u_int64 loopCnt = 0;
    DWORD transmitBuffer[4] = {
        0,
    };
    while (true) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                wprintf(L"\nShutdown requested...\n");
                break;
            }
        }

        client.GetTransmit(transmitBuffer);
        wprintf(L"\n[loop:%I64u] Connect:%d  Disconnect:%d  TPS S/R: %u / %u  "
                L"Verified:%d  PacketPool[T:%d U:%d F:%d]\n",
                loopCnt++, (int)client.ConnectCnt, (int)client.DisconnectCnt, transmitBuffer[2],
                transmitBuffer[0], (int)client.VerifiedCnt, client.LogGetPacketPoolTotal(),
                client.LogGetPacketPoolUse(), client.LogGetPacketPoolFree());
        Sleep(1000);
    }

    // Sender thread 종료 대기
    SetEvent(shutdownEvent);
    WaitForSingleObject(senderHandle, 5000);
    CloseHandle(senderHandle);
    CloseHandle(shutdownEvent);

    // client.Stop()은 ~TestClient → ~NetClient 소멸자가 자동 호출
    delete[] client.States;
    client.States = nullptr;

    return 0;
}
