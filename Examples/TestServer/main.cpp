#include <stdio.h>
#include <conio.h>

#include <string>
#include <vector>

#include "NetServer.h"
#include "MonitoringTool.h"
#include "Config.h"
#include "Statistics.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// Pure echo server — NetServer 순수 throughput·정합성 측정용.
// PacketType 검증 안 함, 받은 payload 그대로 송신.
class EchoServer : public NetServer {
public:
    // 파생 소멸자에서 Stop() — vtable이 아직 EchoServer일 때 호출해야
    // OnClientLeave 콜백이 디스패치된다 (NetServer.h Stop() 주석 참고).
    ~EchoServer() { Stop(); }

    bool OnConnectionRequest() override { return true; }
    void OnClientJoin(unsigned __int64 sessionID) override {}
    void OnClientLeave(unsigned __int64 sessionID) override {}

    void OnRecv(unsigned __int64 sessionID, Packet* packet) override {
        Packet* echo = AllocPacket();
        echo->PutData(packet->GetReadBufferPtr(), packet->GetDataSize());
        SendPacket(sessionID, echo);
        FreePacket(echo);
    }
};

// 1초 주기 샘플. 메모리에 모았다가 정상 종료 시 한 번에 기록한다
// (크래시한 런의 측정치는 어차피 폐기 대상이라 주기 flush를 하지 않는다).
struct Sample {
    std::string Timestamp;
    int ElapsedSec;
    int Connected;
    int FreeSession;
    double RecvTPS;
    double RecvBytes;
    double SendTPS;
    double SendBytes;
    double AcceptTPS;
    int PoolTotal;
    int PoolUse;
    int PoolFree;
    double CpuProcess;
    double CpuProcessor;
    double MemProcessMB;
};

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

}   // namespace


int main(int argc, char* argv[]) {
    std::string exeDir = GetExeDirectory();
    std::string configPath =
        (argc >= 2) ? std::string(argv[1]) : exeDir + "BenchmarkConfig.ini";

    Config config;
    if (!config.Load(configPath.c_str())) {
        printf("[Config] load failed: %s\n", config.GetLastError());
        printf("[Config] path: %s\n", configPath.c_str());
        return 1;
    }

    // --- 설정 읽기 ---------------------------------------------------------
    // TestServer / TestClient 가 같은 ini 를 공유하므로, 서버도 [Client] 키를
    // 모두 읽는다. 읽지 않은 키는 오타로 간주되어 GetUnusedKeys 에 걸린다.
    std::string label = config.GetString("Run", "Label", "run");
    std::string outputDir = config.GetString("Run", "OutputDir", "Benchmark");

    int analysisMinutes = config.GetIntRequired("Analysis", "Minutes");
    int trimMinutes = config.GetIntRequired("Analysis", "Trim");

    int port = config.GetIntRequired("Server", "Port");
    int maxUser = config.GetIntRequired("Server", "MaxUser");
    int workerThreadCnt = config.GetInt("Server", "WorkerThreadCnt", 0);
    bool nagleEnabled = config.GetBoolRequired("Server", "NagleEnabled");
    int idleTimeoutMin = config.GetIntRequired("Server", "IdleTimeout");

    std::string clientServerIP = config.GetStringRequired("Client", "ServerIP");
    int clientServerPort = config.GetIntRequired("Client", "ServerPort");
    int clientCount = config.GetIntRequired("Client", "ClientCount");
    int clientWorkerCnt = config.GetInt("Client", "WorkerThreadCnt", 0);
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
    if (maxUser < clientCount) {
        printf("[Config] Server.MaxUser(%d) must be >= Client.ClientCount(%d)\n", maxUser,
               clientCount);
        return 1;
    }
    if (port != clientServerPort) {
        printf("[Config] Server.Port(%d) != Client.ServerPort(%d)\n", port, clientServerPort);
        return 1;
    }
    if (mode == 2 && idleTimeoutMin * 60000 <= reconnectMax) {
        printf("[Config] warning: Server.IdleTimeout must exceed Client.ReconnectMax\n");
    }
    if (mode == 1 && (lifetimeMin != 0 || lifetimeMax != 0 || reconnectMin != 0 ||
                      reconnectMax != 0)) {
        printf("[Config] warning: Mode 1 ignores Lifetime / Reconnect values\n");
    }

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    int cores = (int)systemInfo.dwNumberOfProcessors;
    if (workerThreadCnt <= 0)
        workerThreadCnt = cores - 1;
    if (workerThreadCnt < 1)
        workerThreadCnt = 1;

    // NetServer::Start 의 nagle 인자는 TCP_NODELAY 값이라 의미가 반대다.
    // (nagle=TRUE -> TCP_NODELAY 설정 -> Nagle 알고리즘 비활성)
    BOOL noDelay = nagleEnabled ? FALSE : TRUE;

    // --- 서버 시작 ---------------------------------------------------------
    EchoServer server;
    if (!server.Start(nullptr, (u_short)port, (u_short)workerThreadCnt, 0, noDelay,
                      (u_short)maxUser)) {
        printf("[TestServer] NetServer Start failed\n");
        return 1;
    }

    SYSTEMTIME startTime;
    GetLocalTime(&startTime);

    printf("\n=== TestServer (pure echo) ===\n");
    printf("Config       : %s\n", configPath.c_str());
    printf("Label        : %s\n", label.c_str());
    printf("Listen       : port %d (expect client from %s)\n", port, clientServerIP.c_str());
    printf("SessionPool  : %d\n", maxUser);
    printf("WorkerThread : server %d / client %d (cores %d)\n", workerThreadCnt, clientWorkerCnt,
           cores);
    printf("Nagle        : %s\n", nagleEnabled ? "enabled" : "disabled (TCP_NODELAY)");
    printf("Load         : N=%d, interval %d-%d ms, mode %d\n", clientCount, sendIntervalMin,
           sendIntervalMax, mode);
    if (analysisMinutes > 0)
        printf("Analysis     : %d min (trim %d min each side)\n", analysisMinutes, trimMinutes);
    else
        printf("Analysis     : unlimited (trim %d min each side)\n", trimMinutes);
    printf("IdleTimeout  : %d min\n", idleTimeoutMin);
    printf("\nPress Q to quit\n\n");

    // --- 측정 루프 ---------------------------------------------------------
    const int idleTimeoutSec = idleTimeoutMin * 60;
    // 안전 상한 — 서버가 멈춰서 무인 실행이 끝나지 않는 사고 방지.
    // 무제한 모드(Minutes=0)에서는 해제한다.
    const int maxDurationSec =
        (analysisMinutes > 0) ? ((analysisMinutes + trimMinutes * 2) * 60 + idleTimeoutSec + 300)
                              : 0;

    std::vector<Sample> samples;
    MonitoringTool monitor;
    DWORD startTick = GetTickCount();
    DWORD transmitBuffer[4] = {
        0,
    };

    int trafficStartSec = -1;
    int lastTrafficSec = -1;
    std::string validity = "OK";

    while (true) {
        if (_kbhit()) {
            int controlKey = _getch();
            if (controlKey == 'q' || controlKey == 'Q') {
                printf("\nShutdown requested...\n");
                break;
            }
        }

        server.GetTransmit(transmitBuffer);
        monitor.UpdateAll();

        SYSTEMTIME now;
        GetLocalTime(&now);

        Sample sample;
        sample.Timestamp = FormatTime(now);
        sample.ElapsedSec = (int)((GetTickCount() - startTick) / 1000);
        sample.Connected = (int)server.GetConnectedSessionCnt();
        sample.FreeSession = (int)server.GetDisconnectedSessionCnt();
        sample.RecvTPS = transmitBuffer[0];
        sample.RecvBytes = transmitBuffer[1];
        sample.SendTPS = transmitBuffer[2];
        sample.SendBytes = transmitBuffer[3];
        sample.AcceptTPS = server.GetAcceptTPS();
        sample.PoolTotal = server.LogGetPacketPoolTotal();
        sample.PoolUse = server.LogGetPacketPoolUse();
        sample.PoolFree = server.LogGetPacketPoolFree();
        sample.CpuProcess = monitor.ProcessTotal();
        sample.CpuProcessor = monitor.ProcessorTotal();
        // 소수점 유지 — 정수 MB로 자르면 짧은 구간에서 반올림 오차가
        // Mem_slopeMBperHour 로 증폭된다 (1MB 차이가 5분 구간에서 12MB/h로 보임)
        sample.MemProcessMB = monitor.ProcessUserAllocMemory() / 1000000.0;
        samples.push_back(sample);

        if (sample.Connected > 0) {
            if (trafficStartSec < 0) {
                trafficStartSec = sample.ElapsedSec;
                printf("[TestServer] traffic started at %d sec\n", trafficStartSec);
            }
            lastTrafficSec = sample.ElapsedSec;
        }

        printf("[%5d s] Conn:%5d  RecvTPS:%7.0f  SendTPS:%7.0f  Pool[T:%d U:%d F:%d]  "
               "CPU:%.1f%%/%.1f%%  Mem:%.1fMB\n",
               sample.ElapsedSec, sample.Connected, sample.RecvTPS, sample.SendTPS,
               sample.PoolTotal, sample.PoolUse, sample.PoolFree, sample.CpuProcess,
               sample.CpuProcessor, sample.MemProcessMB);

        // 종료 판정
        if (trafficStartSec < 0) {
            if (sample.ElapsedSec >= idleTimeoutSec) {
                validity = "INVALID(no connection)";
                printf("\n[TestServer] no connection within %d min - abort\n", idleTimeoutMin);
                break;
            }
        } else if (sample.Connected == 0 && (sample.ElapsedSec - lastTrafficSec) >= idleTimeoutSec) {
            printf("\n[TestServer] traffic idle for %d min - run finished\n", idleTimeoutMin);
            break;
        }

        if (maxDurationSec > 0 && sample.ElapsedSec >= maxDurationSec) {
            validity = "INVALID(max duration)";
            printf("\n[TestServer] max duration reached - abort\n");
            break;
        }

        Sleep(1000);
    }

    // --- 분석 구간 산출 -----------------------------------------------------
    // 트래픽 구간의 앞뒤 Trim분을 제외한다. 유휴 꼬리는 트래픽 구간 밖이라
    // 정의상 자동으로 빠진다.
    const int trimSec = trimMinutes * 60;
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
    std::string samplePath = outDirPath + "\\" + label + "_" + stamp + "_server_samples.csv";
    std::string summaryPath = outDirPath + "\\server_summary.csv";

    FILE* sampleFile = nullptr;
    if (fopen_s(&sampleFile, samplePath.c_str(), "wb") == 0 && sampleFile != nullptr) {
        fwrite("\xEF\xBB\xBF", 1, 3, sampleFile);   // Excel 한글 표시용 BOM
        fprintf(sampleFile, "%s\n",
                u8"Timestamp(시각),ElapsedSec(경과초),Connected(접속수),FreeSession(여유세션),"
                u8"RecvTPS(수신TPS),RecvBytes(수신바이트),SendTPS(송신TPS),SendBytes(송신바이트),"
                u8"AcceptTPS(접속TPS),PoolTotal(풀전체),PoolUse(풀사용),PoolFree(풀여유),"
                u8"CpuProcess(프로세스CPU),CpuProcessor(전체CPU),MemProcessMB(메모리MB)");
        for (const Sample& sample : samples) {
            fprintf(sampleFile, "%s,%d,%d,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%d,%d,%d,%.1f,%.1f,%.1f\n",
                    sample.Timestamp.c_str(), sample.ElapsedSec, sample.Connected,
                    sample.FreeSession, sample.RecvTPS, sample.RecvBytes, sample.SendTPS,
                    sample.SendBytes, sample.AcceptTPS, sample.PoolTotal, sample.PoolUse,
                    sample.PoolFree, sample.CpuProcess, sample.CpuProcessor, sample.MemProcessMB);
        }
        fclose(sampleFile);
        printf("[TestServer] samples -> %s\n", samplePath.c_str());
    } else {
        printf("[TestServer] failed to write sample csv: %s\n", samplePath.c_str());
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
                    u8"CfgClientCount(접속수),CfgSendIntervalMinMs(송신간격최소),"
                    u8"CfgSendIntervalMaxMs(송신간격최대),CfgMode(모드),CfgNagle,"
                    u8"CfgWorkerThreadCnt(워커수),"
                    u8"WindowStartSec(구간시작),WindowEndSec(구간끝),WindowSamples(샘플수),"
                    u8"RecvTPS_avg(평균),RecvTPS_p50(중앙값),RecvTPS_p95,RecvTPS_p99,"
                    u8"RecvTPS_min,RecvTPS_max,SendTPS_avg,"
                    u8"Connected_min(최소접속),Connected_avg(평균접속),"
                    u8"Pool_end(종료시풀),Pool_max(최대풀),"
                    u8"Mem_startMB,Mem_endMB,Mem_maxMB,Mem_slopeMBperHour(시간당증가),"
                    u8"Cpu_avg,Cpu_max,CpuProcessor_avg(전체CPU),Cores(코어수),"
                    u8"DefaultedKeys(기본값사용키)");
        }
        fprintf(summaryFile,
                "%s,%s,%s,%d,%d,%d,%d,%s,%d,%d,%d,%d,"
                "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,"
                "%.0f,%.0f,%.0f,%.0f,"
                "%.1f,%.1f,%.1f,%.2f,"
                "%.1f,%.1f,%.1f,%d,%s\n",
                label.c_str(), FormatTime(startTime).c_str(), validity.c_str(), clientCount,
                sendIntervalMin, sendIntervalMax, mode, nagleEnabled ? "true" : "false",
                workerThreadCnt, windowStartSec, windowEndSec, (int)window.size(),
                Statistics::Average(recvTPS), Statistics::Percentile(recvTPS, 0.50),
                Statistics::Percentile(recvTPS, 0.95), Statistics::Percentile(recvTPS, 0.99),
                Statistics::MinValue(recvTPS), Statistics::MaxValue(recvTPS),
                Statistics::Average(sendTPS), Statistics::MinValue(connectedCnt),
                Statistics::Average(connectedCnt), poolEnd, poolMax, memStart, memEnd, memMax,
                memSlope, Statistics::Average(cpuProcess), Statistics::MaxValue(cpuProcess),
                Statistics::Average(cpuProcessor), cores, config.GetDefaultedKeys().c_str());
        fclose(summaryFile);
        printf("[TestServer] summary -> %s\n", summaryPath.c_str());
    } else {
        printf("[TestServer] failed to write summary csv: %s\n", summaryPath.c_str());
    }

    printf("\n=== Result [%s] ===\n", validity.c_str());
    printf("Window       : %d ~ %d sec (%d samples)\n", windowStartSec, windowEndSec,
           (int)window.size());
    printf("RecvTPS      : avg %.0f / p50 %.0f / p95 %.0f / p99 %.0f\n",
           Statistics::Average(recvTPS), Statistics::Percentile(recvTPS, 0.50),
           Statistics::Percentile(recvTPS, 0.95), Statistics::Percentile(recvTPS, 0.99));
    printf("Memory       : %.1f -> %.1f MB (%.2f MB/hour)\n", memStart, memEnd, memSlope);

    // server.Stop()은 ~EchoServer -> ~NetServer 소멸자가 자동 호출. 명시 호출 불필요.
    return 0;
}
