#pragma comment(lib, "ws2_32")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "NetClient.h"
#include "Packet.h"
#include "Logger.h"
#include "Config.h"
#include "Statistics.h"
#include "MonitoringTool.h"
#include "../TestServer/TestProtocol.h"

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

    // 파생 소멸자에서 Stop() — vtable이 아직 TestClient일 때 호출해야
    // OnDisconnect 콜백이 디스패치된다 (NetClient.h Stop() 주석 참고).
    // 이 시점에는 States 등 파생 멤버도 아직 살아있다 (멤버 소멸은 본체 이후).
    ~TestClient() { Stop(); }

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
        if (type == kTestEcho) {
            VerifiedCnt++;
        } else {
            Logger* pLogger = Logger::GetInstance();
            pLogger->Log(L"TestClient", Logger::LogLevel::kError,
                         L"PacketType mismatch — got 0x%04x, expected kTestEcho(0x%04x), sessionID:%p",
                         type, (WORD)kTestEcho, sessionID);
            pLogger->Crash();
        }
    }

    int N = 0;
    std::unique_ptr<ClientState[]> States;
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

// 1초 주기 샘플. 메모리에 모았다가 정상 종료 시 한 번에 기록한다.
struct Sample {
    std::string Timestamp;
    int ElapsedSec;
    int Connected;
    int ConnectTotal;
    int DisconnectTotal;
    double RecvTPS;
    double RecvBytes;
    double SendTPS;
    double SendBytes;
    int VerifiedTotal;
    int PoolTotal;
    int PoolUse;
    int PoolFree;
    double CpuProcess;
    double CpuProcessor;
    double MemProcessMB;
};

inline DWORD RandomBetween(DWORD min, DWORD max) {
    if (max <= min)
        return min;
    return min + (rand() % (max - min + 1));
}

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

std::string FormatTime(const SYSTEMTIME& time) {
    char buffer[32];
    sprintf_s(buffer, "%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay,
              time.wHour, time.wMinute, time.wSecond);
    return std::string(buffer);
}

std::string FormatFileStamp(const SYSTEMTIME& time) {
    char buffer[32];
    sprintf_s(buffer, "%04d%02d%02d_%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
              time.wMinute, time.wSecond);
    return std::string(buffer);
}

std::string WideToNarrow(const WCHAR* text) {
    char buffer[MAX_PATH] = {
        0,
    };
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, MAX_PATH, nullptr, nullptr);
    return std::string(buffer);
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
    std::string exeDir = GetExeDirectory();
    std::string configPath =
        (argc >= 2) ? WideToNarrow(argv[1]) : exeDir + "BenchmarkConfig.ini";

    Config config;
    if (!config.Load(configPath.c_str())) {
        printf("[Config] load failed: %s\n", config.GetLastError());
        printf("[Config] path: %s\n", configPath.c_str());
        return 1;
    }

    // --- 설정 읽기 ---------------------------------------------------------
    // TestServer 와 같은 ini 를 공유하므로 [Server] 키도 모두 읽는다.
    // 읽지 않은 키는 오타로 간주되어 GetUnusedKeys 에 걸린다.
    std::string label = config.GetString("Run", "Label", "run");
    std::string outputDir = config.GetString("Run", "OutputDir", "Benchmark");

    int analysisMinutes = config.GetIntRequired("Analysis", "Minutes");
    int trimMinutes = config.GetIntRequired("Analysis", "Trim");

    int serverPortCfg = config.GetIntRequired("Server", "Port");
    int maxUser = config.GetIntRequired("Server", "MaxUser");
    int serverWorkerCnt = config.GetInt("Server", "WorkerThreadCnt", 0);
    bool nagleEnabled = config.GetBoolRequired("Server", "NagleEnabled");
    int idleTimeoutMin = config.GetIntRequired("Server", "IdleTimeout");

    std::string serverIP = config.GetStringRequired("Client", "ServerIP");
    int serverPort = config.GetIntRequired("Client", "ServerPort");
    int clientCount = config.GetIntRequired("Client", "ClientCount");
    int workerThreadCnt = config.GetInt("Client", "WorkerThreadCnt", 0);
    int sendIntervalMin = config.GetIntRequired("Client", "SendIntervalMin");
    int sendIntervalMax = config.GetIntRequired("Client", "SendIntervalMax");
    int mode = config.GetIntRequired("Client", "Mode");
    int lifetimeMin = config.GetIntRequired("Client", "LifetimeMin");
    int lifetimeMax = config.GetIntRequired("Client", "LifetimeMax");
    int reconnectMin = config.GetIntRequired("Client", "ReconnectMin");
    int reconnectMax = config.GetIntRequired("Client", "ReconnectMax");

    if (config.HasError()) {
        printf("[Config] %s\n", config.GetLastError());
        return 1;
    }
    std::string unused = config.GetUnusedKeys();
    if (!unused.empty()) {
        printf("[Config] unknown key(s) - check for typo: %s\n", unused.c_str());
        return 1;
    }

    // --- 값 검증 -----------------------------------------------------------
    if (analysisMinutes < 0 || trimMinutes < 0) {
        printf("[Config] Analysis.Minutes / Analysis.Trim must not be negative\n");
        return 1;
    }
    if (analysisMinutes > 0 && trimMinutes * 2 >= analysisMinutes) {
        printf("[Config] Analysis.Trim * 2 must be smaller than Analysis.Minutes\n");
        return 1;
    }
    if (serverPort != serverPortCfg) {
        printf("[Config] Client.ServerPort(%d) != Server.Port(%d)\n", serverPort, serverPortCfg);
        return 1;
    }
    if (maxUser < clientCount) {
        printf("[Config] Server.MaxUser(%d) must be >= Client.ClientCount(%d)\n", maxUser,
               clientCount);
        return 1;
    }
    if (clientCount <= 0 || sendIntervalMin <= 0 || sendIntervalMax < sendIntervalMin) {
        printf("[Config] invalid ClientCount / SendInterval range\n");
        return 1;
    }
    if (mode == 2 && (lifetimeMin <= 0 || lifetimeMax < lifetimeMin ||
                      reconnectMax < reconnectMin)) {
        printf("[Config] invalid Mode 2 Lifetime / Reconnect range\n");
        return 1;
    }
    if (mode == 2 && idleTimeoutMin * 60000 <= reconnectMax) {
        printf("[Config] warning: Server.IdleTimeout must exceed Client.ReconnectMax\n");
    }
    if (mode == 1 && (lifetimeMin != 0 || lifetimeMax != 0 || reconnectMin != 0 ||
                      reconnectMax != 0)) {
        printf("[Config] warning: Mode 1 ignores Lifetime / Reconnect values\n");
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int cores = (int)sysInfo.dwNumberOfProcessors;
    int resolvedWorkerCnt = (workerThreadCnt > 0) ? workerThreadCnt : cores - 1;
    if (resolvedWorkerCnt < 1)
        resolvedWorkerCnt = 1;

    srand((unsigned)GetTickCount());

    // --- 클라이언트 시작 ----------------------------------------------------
    TestClient client;
    if (!client.Start((u_short)resolvedWorkerCnt, 0, (u_short)clientCount)) {
        printf("[TestClient] NetClient Start failed\n");
        return 1;
    }

    client.N = clientCount;
    client.States.reset(new TestClient::ClientState[clientCount]);

    SYSTEMTIME startTime;
    GetLocalTime(&startTime);

    printf("\n=== TestClient ===\n");
    printf("Config       : %s\n", configPath.c_str());
    printf("Label        : %s\n", label.c_str());
    printf("Server       : %s:%d\n", serverIP.c_str(), serverPort);
    printf("WorkerThread : client %d%s / server %s (cores %d)\n", resolvedWorkerCnt,
           (workerThreadCnt > 0) ? "" : " (auto)", (serverWorkerCnt > 0) ? "configured" : "auto",
           cores);
    printf("Nagle        : %s\n", nagleEnabled ? "enabled" : "disabled (TCP_NODELAY)");
    printf("Load         : N=%d, interval %d-%d ms, mode %d\n", clientCount, sendIntervalMin,
           sendIntervalMax, mode);

    // 실행 시간 = 분석 시간 + 앞뒤 trim. Minutes=0 이면 무제한(Q 로 종료).
    const int trimSec = trimMinutes * 60;
    const int runDurationSec =
        (analysisMinutes > 0) ? ((analysisMinutes + trimMinutes * 2) * 60) : 0;
    if (runDurationSec > 0)
        printf("Duration     : %d min (analysis %d min + trim %d min x 2)\n",
               analysisMinutes + trimMinutes * 2, analysisMinutes, trimMinutes);
    else
        printf("Duration     : unlimited (trim %d min each side)\n", trimMinutes);

    DWORD startTick = GetTickCount();
    std::string validity = "OK";

    WCHAR serverIpWide[64];
    MultiByteToWideChar(CP_ACP, 0, serverIP.c_str(), -1, serverIpWide, 64);

    // 초기 N개 connect (main thread)
    printf("\nConnecting %d clients...\n", clientCount);
    for (int i = 0; i < clientCount; i++) {
        unsigned __int64 sid = client.Connect(serverIpWide, (u_short)serverPort);
        if (sid == 0) {
            printf("Initial Connect failed at idx %d\n", i);
            if (i == 0)
                validity = "INVALID(no connection)";
            break;
        }
        client.States[i].currentSessionID.store(sid);
        client.States[i].nextSendTime = startTick;
        client.States[i].lifetimeEnd =
            (mode == 2) ? startTick + RandomBetween(lifetimeMin, lifetimeMax) : 0;
        client.States[i].reconnectAt = startTick;
    }

    // --- Sender thread 시작 -------------------------------------------------
    HANDLE shutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    SenderContext ctx;
    ctx.client = &client;
    strcpy_s(ctx.serverIP, serverIP.c_str());
    ctx.serverPort = serverPort;
    ctx.mode = mode;
    ctx.intervalMin = (DWORD)sendIntervalMin;
    ctx.intervalMax = (DWORD)sendIntervalMax;
    ctx.lifetimeMin = (DWORD)lifetimeMin;
    ctx.lifetimeMax = (DWORD)lifetimeMax;
    ctx.reconnectMin = (DWORD)reconnectMin;
    ctx.reconnectMax = (DWORD)reconnectMax;
    ctx.shutdownEvent = shutdownEvent;

    std::thread senderThread(SenderThread, &ctx);

    printf("\nPress Q to quit\n\n");

    // --- 측정 루프 ---------------------------------------------------------
    std::vector<Sample> samples;
    MonitoringTool monitor;
    DWORD transmitBuffer[4] = {
        0,
    };
    int trafficStartSec = -1;
    int lastTrafficSec = -1;

    while (true) {
        if (_kbhit()) {
            int controlKey = _getch();
            if (controlKey == 'q' || controlKey == 'Q') {
                printf("\nShutdown requested...\n");
                break;
            }
        }

        client.GetTransmit(transmitBuffer);
        monitor.UpdateAll();

        SYSTEMTIME now;
        GetLocalTime(&now);

        Sample sample;
        sample.Timestamp = FormatTime(now);
        sample.ElapsedSec = (int)((GetTickCount() - startTick) / 1000);
        sample.Connected = (int)client.GetConnectedSessionCnt();
        sample.ConnectTotal = client.ConnectCnt.load();
        sample.DisconnectTotal = client.DisconnectCnt.load();
        sample.RecvTPS = transmitBuffer[0];
        sample.RecvBytes = transmitBuffer[1];
        sample.SendTPS = transmitBuffer[2];
        sample.SendBytes = transmitBuffer[3];
        sample.VerifiedTotal = client.VerifiedCnt.load();
        sample.PoolTotal = client.LogGetPacketPoolTotal();
        sample.PoolUse = client.LogGetPacketPoolUse();
        sample.PoolFree = client.LogGetPacketPoolFree();
        sample.CpuProcess = monitor.ProcessTotal();
        sample.CpuProcessor = monitor.ProcessorTotal();
        sample.MemProcessMB = (double)(monitor.ProcessUserAllocMemory() / 1000000);
        samples.push_back(sample);

        if (sample.Connected > 0) {
            if (trafficStartSec < 0)
                trafficStartSec = sample.ElapsedSec;
            lastTrafficSec = sample.ElapsedSec;
        }

        printf("[%5d s] Conn:%5d  RecvTPS:%7.0f  SendTPS:%7.0f  Verified:%d  Pool[T:%d U:%d F:%d]  "
               "CPU:%.1f%%/%.1f%%  Mem:%.0fMB\n",
               sample.ElapsedSec, sample.Connected, sample.RecvTPS, sample.SendTPS,
               sample.VerifiedTotal, sample.PoolTotal, sample.PoolUse, sample.PoolFree,
               sample.CpuProcess, sample.CpuProcessor, sample.MemProcessMB);

        if (runDurationSec > 0 && sample.ElapsedSec >= runDurationSec) {
            printf("\n[TestClient] duration reached - run finished\n");
            break;
        }

        Sleep(1000);
    }

    // Sender thread 종료 대기 — shutdownEvent SetEvent 후 sender가 종료될 때까지 join
    SetEvent(shutdownEvent);
    if (senderThread.joinable())
        senderThread.join();
    CloseHandle(shutdownEvent);

    // --- 분석 구간 산출 -----------------------------------------------------
    int windowStartSec = 0;
    int windowEndSec = 0;
    std::vector<Sample> window;

    if (trafficStartSec >= 0 && lastTrafficSec > trafficStartSec) {
        windowStartSec = trafficStartSec + trimSec;
        windowEndSec = lastTrafficSec - trimSec;
        for (const Sample& sample : samples) {
            if (sample.ElapsedSec >= windowStartSec && sample.ElapsedSec <= windowEndSec)
                window.push_back(sample);
        }
    }
    if (window.empty() && validity == "OK")
        validity = "INVALID(window too short)";

    std::vector<double> recvTPS, sendTPS, connectedCnt, cpuProcess, cpuProcessor;
    for (const Sample& sample : window) {
        recvTPS.push_back(sample.RecvTPS);
        sendTPS.push_back(sample.SendTPS);
        connectedCnt.push_back((double)sample.Connected);
        cpuProcess.push_back(sample.CpuProcess);
        cpuProcessor.push_back(sample.CpuProcessor);
    }

    double memStart = window.empty() ? 0.0 : window.front().MemProcessMB;
    double memEnd = window.empty() ? 0.0 : window.back().MemProcessMB;
    double memMax = 0.0;
    double poolMax = 0.0;
    double poolEnd = window.empty() ? 0.0 : window.back().PoolUse;
    for (const Sample& sample : window) {
        if (sample.MemProcessMB > memMax)
            memMax = sample.MemProcessMB;
        if (sample.PoolUse > poolMax)
            poolMax = sample.PoolUse;
    }
    double windowHours = window.empty() ? 0.0 : (double)(windowEndSec - windowStartSec) / 3600.0;
    double memSlope = (windowHours > 0.0) ? (memEnd - memStart) / windowHours : 0.0;

    // --- CSV 출력 ----------------------------------------------------------
    std::string outDirPath = exeDir + outputDir;
    CreateDirectoryA(outDirPath.c_str(), nullptr);

    std::string stamp = FormatFileStamp(startTime);
    std::string samplePath = outDirPath + "\\" + label + "_" + stamp + "_client_samples.csv";
    std::string summaryPath = outDirPath + "\\client_summary.csv";

    FILE* sampleFile = nullptr;
    if (fopen_s(&sampleFile, samplePath.c_str(), "wb") == 0 && sampleFile != nullptr) {
        fwrite("\xEF\xBB\xBF", 1, 3, sampleFile);   // Excel 한글 표시용 BOM
        fprintf(sampleFile, "%s\n",
                u8"Timestamp(시각),ElapsedSec(경과초),Connected(접속수),"
                u8"ConnectTotal(누적접속),DisconnectTotal(누적끊김),"
                u8"RecvTPS(수신TPS),RecvBytes(수신바이트),SendTPS(송신TPS),SendBytes(송신바이트),"
                u8"VerifiedTotal(검증완료),PoolTotal(풀전체),PoolUse(풀사용),PoolFree(풀여유),"
                u8"CpuProcess(프로세스CPU),CpuProcessor(전체CPU),MemProcessMB(메모리MB)");
        for (const Sample& sample : samples) {
            fprintf(sampleFile,
                    "%s,%d,%d,%d,%d,%.0f,%.0f,%.0f,%.0f,%d,%d,%d,%d,%.1f,%.1f,%.0f\n",
                    sample.Timestamp.c_str(), sample.ElapsedSec, sample.Connected,
                    sample.ConnectTotal, sample.DisconnectTotal, sample.RecvTPS, sample.RecvBytes,
                    sample.SendTPS, sample.SendBytes, sample.VerifiedTotal, sample.PoolTotal,
                    sample.PoolUse, sample.PoolFree, sample.CpuProcess, sample.CpuProcessor,
                    sample.MemProcessMB);
        }
        fclose(sampleFile);
        printf("[TestClient] samples -> %s\n", samplePath.c_str());
    } else {
        printf("[TestClient] failed to write sample csv: %s\n", samplePath.c_str());
    }

    bool summaryExists = false;
    FILE* probe = nullptr;
    if (fopen_s(&probe, summaryPath.c_str(), "rb") == 0 && probe != nullptr) {
        summaryExists = true;
        fclose(probe);
    }

    FILE* summaryFile = nullptr;
    if (fopen_s(&summaryFile, summaryPath.c_str(), "ab") == 0 && summaryFile != nullptr) {
        if (!summaryExists) {
            fwrite("\xEF\xBB\xBF", 1, 3, summaryFile);
            fprintf(summaryFile, "%s\n",
                    u8"Label(런이름),StartTime(시작시각),Valid(유효),"
                    u8"CfgClientCount(접속수),CfgSendInterval(송신간격ms),CfgMode(모드),CfgNagle,"
                    u8"CfgWorkerThreadCnt(워커수),"
                    u8"WindowStartSec(구간시작),WindowEndSec(구간끝),WindowSamples(샘플수),"
                    u8"RecvTPS_avg(평균),RecvTPS_p50(중앙값),RecvTPS_p95,RecvTPS_p99,"
                    u8"RecvTPS_min,RecvTPS_max,SendTPS_avg,"
                    u8"Connected_min(최소접속),Connected_avg(평균접속),"
                    u8"ConnectTotal(누적접속),DisconnectTotal(누적끊김),VerifiedTotal(검증완료),"
                    u8"Pool_end(종료시풀),Pool_max(최대풀),"
                    u8"Mem_startMB,Mem_endMB,Mem_maxMB,Mem_slopeMBperHour(시간당증가),"
                    u8"Cpu_avg,Cpu_max,CpuProcessor_avg(전체CPU),Cores(코어수),"
                    u8"DefaultedKeys(기본값사용키)");
        }
        fprintf(summaryFile,
                "%s,%s,%s,%d,%d-%d,%d,%s,%d,%d,%d,%d,"
                "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,"
                "%.0f,%.0f,%d,%d,%d,"
                "%.0f,%.0f,"
                "%.0f,%.0f,%.0f,%.2f,"
                "%.1f,%.1f,%.1f,%d,%s\n",
                label.c_str(), FormatTime(startTime).c_str(), validity.c_str(), clientCount,
                sendIntervalMin, sendIntervalMax, mode, nagleEnabled ? "true" : "false",
                resolvedWorkerCnt, windowStartSec, windowEndSec, (int)window.size(),
                Statistics::Average(recvTPS), Statistics::Percentile(recvTPS, 0.50),
                Statistics::Percentile(recvTPS, 0.95), Statistics::Percentile(recvTPS, 0.99),
                Statistics::MinValue(recvTPS), Statistics::MaxValue(recvTPS),
                Statistics::Average(sendTPS), Statistics::MinValue(connectedCnt),
                Statistics::Average(connectedCnt), client.ConnectCnt.load(),
                client.DisconnectCnt.load(), client.VerifiedCnt.load(), poolEnd, poolMax, memStart,
                memEnd, memMax, memSlope, Statistics::Average(cpuProcess),
                Statistics::MaxValue(cpuProcess), Statistics::Average(cpuProcessor), cores,
                config.GetDefaultedKeys().c_str());
        fclose(summaryFile);
        printf("[TestClient] summary -> %s\n", summaryPath.c_str());
    } else {
        printf("[TestClient] failed to write summary csv: %s\n", summaryPath.c_str());
    }

    printf("\n=== Result [%s] ===\n", validity.c_str());
    printf("Window       : %d ~ %d sec (%d samples)\n", windowStartSec, windowEndSec,
           (int)window.size());
    printf("RecvTPS      : avg %.0f / p50 %.0f / p95 %.0f / p99 %.0f\n",
           Statistics::Average(recvTPS), Statistics::Percentile(recvTPS, 0.50),
           Statistics::Percentile(recvTPS, 0.95), Statistics::Percentile(recvTPS, 0.99));
    printf("Verified     : %d (recv %d)\n", client.VerifiedCnt.load(), client.RecvCnt.load());
    printf("Memory       : %.0f -> %.0f MB (%.2f MB/hour)\n", memStart, memEnd, memSlope);

    // client.Stop()은 ~TestClient → ~NetClient 소멸자가 자동 호출
    // States는 unique_ptr — ~TestClient 시점에 자동 해제

    return 0;
}
