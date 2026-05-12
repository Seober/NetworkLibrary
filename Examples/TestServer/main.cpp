#include <stdio.h>
#include <conio.h>

#include "NetServer.h"
#include "MonitoringTool.h"


#define DEFAULT_PORT 12001
#define MAX_USER 16000

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// Pure echo server — NetServer 순수 throughput·정합성 측정용.
// PacketType 검증 안 함, 받은 payload 그대로 송신.
class EchoServer : public NetServer {
public:
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

}  // namespace


int main(void) {
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    int processorCnt = systemInfo.dwNumberOfProcessors - 1;

    BOOL nagleFlag;
    wprintf(L"Input nagleFlag[0:OFF, 1:ON] : ");
    scanf_s("%d", &nagleFlag);
    if (nagleFlag)
        nagleFlag = FALSE;
    else
        nagleFlag = TRUE;

    EchoServer server;
    if (!server.Start(NULL, DEFAULT_PORT, processorCnt, 0, nagleFlag, MAX_USER * 2))
        return 0;

    wprintf(L"\n=== TestServer (pure echo) Listening on port %d ===\n\n", DEFAULT_PORT);
    wprintf(L"Press Q to quit\n\n");

    WCHAR controlKey;
    u_int64 loopCnt = 0;
    DWORD transmitBuffer[4] = {
        0,
    };
    MonitoringTool monitor;

    while (1) {
        if (_kbhit()) {
            controlKey = _getwch();
            if (controlKey == L'q' || controlKey == L'Q')
                break;
        }

        server.GetTransmit(transmitBuffer);
        monitor.UpdateAll();

        wprintf(L"\n====================================[loopCnt:%I64u]================================"
                L"=====\n",
                loopCnt++);
        wprintf(L"SessionPool[Capacity:%d / Connected:%d / Free:%d]\n",
                server.GetTotalSessionCnt(), server.GetConnectedSessionCnt(),
                server.GetDisconnectedSessionCnt());
        wprintf(L"[Network]\t> RecvTPS:%d, RecvBytes:%d kB\n", transmitBuffer[0],
                transmitBuffer[1] / 1000);
        wprintf(L"[Network]\t> SendTPS:%d, SendBytes:%d kB\n", transmitBuffer[2],
                transmitBuffer[3] / 1000);
        wprintf(L"[Network]\t> AcceptTotal:%I64u\tAcceptTPS:%d\n", server.GetAcceptTotal(),
                server.GetAcceptTPS());
        wprintf(L"[PacketPool]\t> Total:%d\tUse:%d\tFree:%d\n", server.LogGetPacketPoolTotal(),
                server.LogGetPacketPoolUse(), server.LogGetPacketPoolFree());
        wprintf(L"[CPU]\t\t> Process:%.1f%% (User:%.1f%% Kernel:%.1f%%)  Processor:%.1f%%\n",
                monitor.ProcessTotal(), monitor.ProcessUser(), monitor.ProcessKernel(),
                monitor.ProcessorTotal());
        wprintf(L"[Memory]\t> Process:%uMB  Available:%dMB\n",
                monitor.ProcessUserAllocMemory() / 1000000, monitor.AvailableMemory());
        wprintf(L"[NIC]\t\t> Recv:%dKB Send:%dKB\n",
                (int)(monitor.NetworkRecvBytes() / 1000),
                (int)(monitor.NetworkSendBytes() / 1000));
        wprintf(L"===================================================================================="
                L"\n");

        Sleep(1000);
    }

    // server.Stop()은 ~EchoServer → ~NetServer 소멸자가 자동 호출. 명시 호출 불필요.
    return 0;
}
