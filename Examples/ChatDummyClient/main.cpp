#define FD_SETSIZE 1024
#pragma comment(lib, "ws2_32")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <thread>
#include <vector>

#include "WorkerThread.h"
#include "ClientSession.h"
#include "XorPacketEncoder.h"

#include "Config.h"

namespace {

std::string GetExeDirectory(void) {
    char path[MAX_PATH] = {
        0,
    };
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    std::string full(path);
    std::size_t pos = full.find_last_of("\\/");
    if (pos == std::string::npos)
        return std::string();
    return full.substr(0, pos + 1);
}

std::string WideToNarrow(const WCHAR* text) {
    char buffer[MAX_PATH] = {
        0,
    };
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, MAX_PATH, nullptr, nullptr);
    return std::string(buffer);
}

}   // namespace

int wmain(int argc, WCHAR* argv[]) {
    // 설정 - 실행 파일 옆 ChatConfig.ini (argv[1] 로 경로 지정 가능)
    std::string exeDir = GetExeDirectory();
    std::string configPath = (argc >= 2) ? WideToNarrow(argv[1]) : exeDir + "ChatConfig.ini";

    Config config;
    if (!config.Load(configPath.c_str())) {
        wprintf(L"[Config] load failed: %hs\n", config.GetLastError());
        wprintf(L"[Config] path: %hs\n", configPath.c_str());
        return 1;
    }

    // --- 설정 읽기 ---------------------------------------------------------
    // ChatServer 와 같은 ini 를 공유하므로 [Server] 키도 모두 읽는다.
    // 읽지 않은 키는 오타로 간주되어 GetUnusedKeys 에 걸린다.
    std::string listenIP = config.GetStringRequired("Server", "ListenIP");
    int serverListenPort = config.GetIntRequired("Server", "Port");
    int maxUser = config.GetIntRequired("Server", "MaxUser");
    int serverWorkerThreadCnt = config.GetIntRequired("Server", "WorkerThreadCnt");
    bool nagleEnabled = config.GetBoolRequired("Server", "NagleEnabled");

    std::string serverIP = config.GetStringRequired("Client", "ServerIP");
    int serverPort = config.GetIntRequired("Client", "ServerPort");
    int N = config.GetIntRequired("Client", "ClientCount");
    int messageInterval = config.GetIntRequired("Client", "MessageInterval");
    int lifetimeMin = config.GetIntRequired("Client", "LifetimeMin");
    int lifetimeMax = config.GetIntRequired("Client", "LifetimeMax");
    int sessionPerThread = config.GetIntRequired("Client", "SessionPerThread");

    if (config.HasError()) {
        wprintf(L"[Config] %hs\n", config.GetLastError());
        return 1;
    }
    std::string unused = config.GetUnusedKeys();
    if (!unused.empty()) {
        wprintf(L"[Config] unknown key(s) - check for typo: %hs\n", unused.c_str());
        return 1;
    }

    // --- 값 검증 -----------------------------------------------------------
    if (serverListenPort <= 0 || serverListenPort > 65535 || serverPort <= 0 ||
        serverPort > 65535) {
        wprintf(L"[Config] Port / ServerPort must be 1-65535\n");
        return 1;
    }
    if (serverListenPort != serverPort) {
        wprintf(L"[Config] Server.Port(%d) != Client.ServerPort(%d)\n", serverListenPort,
                serverPort);
        return 1;
    }
    // 세션 풀로 MaxUser*2 를 넘기는데 Start 의 connectSession_Max 가 u_short 라
    // 32767 을 넘기면 값이 조용히 잘린다.
    if (maxUser < 1 || maxUser > 32767) {
        wprintf(L"[Config] Server.MaxUser(%d) must be 1-32767\n", maxUser);
        return 1;
    }
    if (N < 1 || messageInterval < 1 || sessionPerThread < 1) {
        wprintf(L"[Config] Client.ClientCount / MessageInterval / SessionPerThread must be "
                L"positive\n");
        return 1;
    }
    if (maxUser < N) {
        wprintf(L"[Config] Server.MaxUser(%d) must be >= Client.ClientCount(%d)\n", maxUser, N);
        return 1;
    }
    if (serverWorkerThreadCnt < 0 || lifetimeMin < 0 || lifetimeMax < 0) {
        wprintf(L"[Config] WorkerThreadCnt / LifetimeMin / LifetimeMax must not be negative\n");
        return 1;
    }
    if (lifetimeMin > 0 && lifetimeMax <= lifetimeMin) {
        wprintf(L"[Config] Client.LifetimeMax must be greater than LifetimeMin\n");
        return 1;
    }
    if (lifetimeMin == 0 && lifetimeMax != 0)
        wprintf(L"[Config] warning: LifetimeMin 0 disables disconnect - LifetimeMax ignored\n");

    DWORD minLifetime = (DWORD)lifetimeMin;
    DWORD maxLifetime = (DWORD)lifetimeMax;

    wprintf(L"\n=== DummyClient ===\n");
    wprintf(L"Config       : %hs\n", configPath.c_str());
    wprintf(L"Server       : %hs:%d\n", serverIP.c_str(), serverPort);
    wprintf(L"Server(cfg)  : listen %hs, MaxUser %d, worker %hs, Nagle %hs\n", listenIP.c_str(),
            maxUser, (serverWorkerThreadCnt > 0) ? "configured" : "auto",
            nagleEnabled ? "enabled" : "disabled");
    if (!config.GetDefaultedKeys().empty())
        wprintf(L"Defaulted    : %hs\n", config.GetDefaultedKeys().c_str());

    // 프로세서 수 자동 감지 + 스레드/세션 분배
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD processorCount = sysInfo.dwNumberOfProcessors;

    int threadCount = (N + sessionPerThread - 1) / sessionPerThread;
    if (threadCount > (int)processorCount)
        threadCount = (int)processorCount;
    if (threadCount < 1)
        threadCount = 1;

    int basePerThread = N / threadCount;
    int remainder = N % threadCount;

    wprintf(L"\nTotal Clients: %d\n", N);
    wprintf(L"Processors: %u, Worker Threads: %d\n", processorCount, threadCount);
    wprintf(L"Per-thread: %d (+%d for first %d threads)\n", basePerThread, remainder ? 1 : 0,
            remainder);
    wprintf(L"Message Interval: %d ms\n", messageInterval);
    wprintf(L"Press Q to quit\n\n");

    // WSAStartup
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        wprintf(L"WSAStartup failed\n");
        return 1;
    }

    // Server addr
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)serverPort);
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) != 1) {
        wprintf(L"Invalid IP: %hs\n", serverIP.c_str());
        WSACleanup();
        return 1;
    }

    // 세션 배열 할당
    ClientSession* sessions = (ClientSession*)calloc(N, sizeof(ClientSession));
    if (sessions == nullptr) {
        wprintf(L"Memory alloc failed\n");
        WSACleanup();
        return 1;
    }
    for (int i = 0; i < N; i++) {
        sessions[i].sock = INVALID_SOCKET;
        sessions[i].state = SessionState::kIdle;
    }

    // 공유 통계
    volatile LONG connected = 0, active = 0, failed = 0, disconnected = 0;
    volatile LONG tps_sent = 0, tps_recv = 0;
    volatile LONG64 totalSent = 0, totalRecv = 0;

    // shutdown event
    HANDLE shutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // 인코더 — 모든 워커 스레드가 공유. main 스택에서 생성, 워커보다 오래 살아있음 보장.
    // 디폴트는 ChatServer와 같은 XOR (0x77, 0x32). 다른 알고리즘 사용 시 IPacketEncoder 상속해서 인스턴스만 교체.
    XorPacketEncoder encoder(0x77, 0x32);

    // 워커 컨텍스트 + 스레드 생성
    WorkerContext* ctxArr = (WorkerContext*)calloc(threadCount, sizeof(WorkerContext));
    std::vector<std::thread> workerThreads;
    workerThreads.reserve(threadCount);

    int sessionIdx = 0;
    for (int t = 0; t < threadCount; t++) {
        int thisCount = basePerThread + (t < remainder ? 1 : 0);

        ctxArr[t].threadId = t;
        ctxArr[t].sessions = &sessions[sessionIdx];
        ctxArr[t].sessionCount = thisCount;
        ctxArr[t].sessionStartIdx = sessionIdx;
        ctxArr[t].serverAddr = serverAddr;
        ctxArr[t].messageInterval = (DWORD)messageInterval;
        ctxArr[t].minLifetimeMs = minLifetime;
        ctxArr[t].maxLifetimeMs = maxLifetime;
        ctxArr[t].shutdownEvent = shutdownEvent;
        ctxArr[t].encoder = &encoder;
        ctxArr[t].Connected = &connected;
        ctxArr[t].Active = &active;
        ctxArr[t].Failed = &failed;
        ctxArr[t].Disconnected = &disconnected;
        ctxArr[t].TPS_Sent = &tps_sent;
        ctxArr[t].TPS_Recv = &tps_recv;
        ctxArr[t].TotalSent = &totalSent;
        ctxArr[t].TotalRecv = &totalRecv;

        workerThreads.emplace_back(WorkerThreadFunc, &ctxArr[t]);

        sessionIdx += thisCount;
    }

    // 통계 출력 + Q 입력 감지 루프
    DWORD startTick = GetTickCount();
    while (true) {
        Sleep(1000);

        LONG snap_connected = connected;
        LONG snap_active = active;
        LONG snap_failed = failed;
        LONG snap_disconnected = disconnected;
        LONG snap_tps_sent = InterlockedExchange(&tps_sent, 0);
        LONG snap_tps_recv = InterlockedExchange(&tps_recv, 0);
        LONG64 snap_totalSent = totalSent;
        LONG64 snap_totalRecv = totalRecv;

        DWORD elapsed = (GetTickCount() - startTick) / 1000;

        wprintf(
            L"[%us] Conn:%d  Active:%d  Failed:%d  DC:%d  | TPS S/R: %d / %d  Total S/R: %lld / "
            L"%lld bytes\n",
            elapsed, snap_connected, snap_active, snap_failed, snap_disconnected, snap_tps_sent,
            snap_tps_recv, snap_totalSent, snap_totalRecv);

        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                wprintf(L"\nShutdown requested...\n");
                SetEvent(shutdownEvent);
                break;
            }
        }
    }

    // 워커 종료 대기 — shutdownEvent SetEvent 후 모든 worker가 종료될 때까지 join
    for (auto& t : workerThreads)
        if (t.joinable())
            t.join();

    CloseHandle(shutdownEvent);

    free(ctxArr);
    free(sessions);

    WSACleanup();

    wprintf(L"Done.\n");
    return 0;
}
