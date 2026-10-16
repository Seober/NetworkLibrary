#include <stdio.h>
#include <conio.h>
#include <time.h>

#include <string>

#include "Content.h"

#include "Config.h"
#include "Logger.h"
#include "MonitoringTool.h"


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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int wmain(int argc, WCHAR* argv[]) {
    Logger* logger = Logger::GetInstance();

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
    // ChatDummyClient 와 같은 ini 를 공유하므로 [Client] 키도 모두 읽는다.
    // 읽지 않은 키는 오타로 간주되어 GetUnusedKeys 에 걸린다.
    std::string listenIP = config.GetStringRequired("Server", "ListenIP");
    int port = config.GetIntRequired("Server", "Port");
    int maxUser = config.GetIntRequired("Server", "MaxUser");
    int workerThreadCnt = config.GetIntRequired("Server", "WorkerThreadCnt");
    bool nagleEnabled = config.GetBoolRequired("Server", "NagleEnabled");

    std::string clientServerIP = config.GetStringRequired("Client", "ServerIP");
    int clientServerPort = config.GetIntRequired("Client", "ServerPort");
    int clientCount = config.GetIntRequired("Client", "ClientCount");
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
    if (port <= 0 || port > 65535 || clientServerPort <= 0 || clientServerPort > 65535) {
        wprintf(L"[Config] Port / ServerPort must be 1-65535\n");
        return 1;
    }
    if (port != clientServerPort) {
        wprintf(L"[Config] Server.Port(%d) != Client.ServerPort(%d)\n", port, clientServerPort);
        return 1;
    }
    // 세션 풀로 MaxUser*2 를 넘기는데 Start 의 connectSession_Max 가 u_short 라
    // 32767 을 넘기면 값이 조용히 잘린다.
    if (maxUser < 1 || maxUser > 32767) {
        wprintf(L"[Config] Server.MaxUser(%d) must be 1-32767\n", maxUser);
        return 1;
    }
    if (clientCount < 1 || messageInterval < 1 || sessionPerThread < 1) {
        wprintf(L"[Config] Client.ClientCount / MessageInterval / SessionPerThread must be "
                L"positive\n");
        return 1;
    }
    if (maxUser < clientCount) {
        wprintf(L"[Config] Server.MaxUser(%d) must be >= Client.ClientCount(%d)\n", maxUser,
                clientCount);
        return 1;
    }
    if (workerThreadCnt < 0 || lifetimeMin < 0 || lifetimeMax < 0) {
        wprintf(L"[Config] WorkerThreadCnt / LifetimeMin / LifetimeMax must not be negative\n");
        return 1;
    }
    if (lifetimeMin > 0 && lifetimeMax <= lifetimeMin) {
        wprintf(L"[Config] Client.LifetimeMax must be greater than LifetimeMin\n");
        return 1;
    }
    if (lifetimeMin == 0 && lifetimeMax != 0)
        wprintf(L"[Config] warning: LifetimeMin 0 disables disconnect - LifetimeMax ignored\n");

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);

    int cores = (int)systemInfo.dwNumberOfProcessors;
    int processorCnt = (workerThreadCnt > 0) ? workerThreadCnt : cores - 1;
    if (processorCnt < 1)
        processorCnt = 1;

    // NetServer::Start 의 nagle 인자는 TCP_NODELAY 값이라 의미가 반대다.
    // (nagle=TRUE -> TCP_NODELAY 설정 -> Nagle 알고리즘 비활성)
    BOOL noDelay = nagleEnabled ? FALSE : TRUE;

    // ListenIP 의 0.0.0.0 은 INADDR_ANY sentinel - Config 가 빈 값을 허용하지 않아
    // "전 인터페이스"를 표현할 다른 방법이 없다.
    WCHAR listenIPWide[64] = {
        0,
    };
    const WCHAR* listenIPArg = nullptr;
    if (listenIP != "0.0.0.0") {
        MultiByteToWideChar(CP_ACP, 0, listenIP.c_str(), -1, listenIPWide, 64);
        listenIPArg = listenIPWide;
    }

    wprintf(L"\n=== ChatServer ===\n");
    wprintf(L"Config       : %hs\n", configPath.c_str());
    wprintf(L"Listen       : %hs:%d\n", listenIP.c_str(), port);
    wprintf(L"MaxUser      : %d (session pool %d)\n", maxUser, maxUser * 2);
    wprintf(L"WorkerThread : %d%hs (cores %d)\n", processorCnt,
            (workerThreadCnt > 0) ? "" : " (auto)", cores);
    wprintf(L"Nagle        : %hs\n", nagleEnabled ? "enabled" : "disabled (TCP_NODELAY)");
    wprintf(L"Client(cfg)  : %hs N=%d, interval %d ms, lifetime %d-%d ms, %d per thread\n",
            clientServerIP.c_str(), clientCount, messageInterval, lifetimeMin, lifetimeMax,
            sessionPerThread);
    if (!config.GetDefaultedKeys().empty())
        wprintf(L"Defaulted    : %hs\n", config.GetDefaultedKeys().c_str());
    wprintf(L"\nPress Q to quit\n\n");

    ChatServer server(maxUser);
    /*server.CreateContentThread(true);*/
    if (!server.Start(listenIPArg, (u_short)port, (u_short)processorCnt, 0, noDelay,
                      (u_short)(maxUser * 2)))
        return 0;

    DWORD mainThreadID = GetCurrentThreadId();
    WCHAR controlKey;


    u_int64 loopCnt = 0;
    MonitoringTool monitor;

    DWORD transmitBuffer[4] = {
        0,
    };
    int processUseCPUTotal;
    int processUseMemory;

    int sessionCnt;
    int userCnt;
    int jobTPS;
    int usePacketPool;
    int useJobPool;

    int processorUseCPUTotal;
    int processorNonPagedMemory;
    int processorNetworkRecv;
    int processorNetworkSend;
    int processorAvailableMemory;

    int curTime;

    while (1) {
        if (_kbhit()) {
            controlKey = _getwch();
            if (controlKey == L'q' || controlKey == L'Q')
                break;
        }

        server.GetTransmit(transmitBuffer);
        monitor.UpdateAll();

        processUseCPUTotal = (int)monitor.ProcessTotal();
        processUseMemory = monitor.ProcessUserAllocMemory() / 1000000;

        sessionCnt = server.GetConnectedSessionCnt();
        userCnt = server.GetCharacterSize();
        jobTPS = server.GetJobTPS();
        usePacketPool = server.LogGetPacketPoolUse();
        useJobPool = server.LogGetJobPoolUse();

        processorUseCPUTotal = (int)monitor.ProcessorTotal();
        processorNonPagedMemory = monitor.NonpagedMemory() / 1000000;
        processorNetworkRecv = (int)(monitor.NetworkRecvBytes() / 1000);
        processorNetworkSend = (int)(monitor.NetworkSendBytes() / 1000);
        processorAvailableMemory = monitor.AvailableMemory();

        time((time_t*)&curTime);

        wprintf(
            L"\n====================================[loopCnt:%I64u]================================"
            L"=====\n",
            loopCnt++);
        wprintf(L"sessionCnt:%d, CharacterCnt:%d\n\n", sessionCnt, userCnt);
        wprintf(L"[Network_Logic]\t> RecvTPS:%d, RecvBytes:%d kB\n", transmitBuffer[0],
                transmitBuffer[1] / 1000);
        wprintf(L"[Network_Logic]\t> SendTPS:%d, SendBytes:%d kB\n", transmitBuffer[2],
                transmitBuffer[3] / 1000);
        wprintf(L"[Network_Logic]\t> AcceptTotal:%I64u\t\tAcceptTPS:%d\n\n",
                server.GetAcceptTotal(), server.GetAcceptTPS());
        wprintf(L"[ChunckPool_Packet]\t> Total:%d\tUse:%d\t\tFree:%d\n",
                server.LogGetPacketPoolTotal(), server.LogGetPacketPoolUse(),
                server.LogGetPacketPoolFree());
        wprintf(L"[ChunckPool_Job]\t> Total:%d\tUse:%d\t\tFree:%d\n\n",
                server.LogGetJobPoolTotal(), server.LogGetJobPoolUse(),
                server.LogGetJobPoolFree());
        wprintf(L"[JobQueue]\t> RemainJob:%d\tJob/s:%d\n", server.RemainJob(), jobTPS);
        wprintf(L"[UpdateThread]\t> SleepTime/s:%d ms\tRunningTime/s:%d\n",
                server.GetThreadRunningTime(), server.GetThreadRunningTPS());
        wprintf(
            L"===================================================================================="
            L"\n");

        wprintf(L"[CPU]\t\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n", monitor.ProcessorTotal(),
                monitor.ProcessorUser(), monitor.ProcessorKernel());
        wprintf(L"[Process]\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n\n", monitor.ProcessTotal(),
                monitor.ProcessUser(), monitor.ProcessKernel());

        wprintf(L"[Process]\t> UserAllocMemory:%u MB\t\tNonPagedMemory:%d MB\n", processUseMemory,
                monitor.ProcessNonPagedMemory() / 1000000);
        wprintf(L"[PC]\t\t> AvailableMemory:%d MB\t\tNonPagedMemory:%d MB\n\n",
                processorAvailableMemory, processorNonPagedMemory);

        wprintf(L"[Network]\t> RecvBytes:%d kB\t\tSendBytes:%d kB\n", processorNetworkRecv,
                processorNetworkSend);
        wprintf(
            L"===================================================================================="
            L"\n");
        wprintf(L"\n\n\n");

        Sleep(1000);
    }


    return 0;
}