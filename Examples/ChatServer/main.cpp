#include <stdio.h>
#include <conio.h>
#include <time.h>

#include "Content.h"

#include "Logger.h"
#include "MonitoringTool.h"


#define SERVER_IP NULL
#define SERVER_PORT 12001

#define MAX_USER 16000

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void) {
    Logger* logger = Logger::GetInstance();
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

    ChatServer server(MAX_USER);
    /*server.CreateContentThread(true);*/
    if (!server.Start(SERVER_IP, SERVER_PORT, processorCnt, 0, nagleFlag, MAX_USER * 2))
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
        useJobPool = server.Log_GetJobPoolUse();

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
                server.Log_GetJobPoolTotal(), server.Log_GetJobPoolUse(),
                server.Log_GetJobPoolFree());
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


        //server.GetTransmit(transmitBuffer);

        //wprintf(L"\n====================================[loopCnt:%I64u]=====================================\n", loopCnt++);
        //wprintf(L"sessionCnt:%d, CharacterCnt:%d\n\n", server.GetConnectedSessionCnt(), server.GetCharacterSize());
        //wprintf(L"[Network_Logic]\t> RecvTPS:%d, RecvBytes:%d B\n", transmitBuffer[0], transmitBuffer[1]);
        //wprintf(L"[Network_Logic]\t> SendTPS:%d, SendBytes:%d B\n", transmitBuffer[2], transmitBuffer[3]);
        //wprintf(L"[Network_Logic]\t> AcceptTotal:%I64u\t\tAcceptTPS:%d\n\n", server.GetAcceptTotal(), server.GetAcceptTPS());
        //wprintf(L"[ChunckPool_Packet]\t> Total:%d\tUse:%d\tFree:%d\n", server.LogGetPacketPoolTotal(), server.LogGetPacketPoolUse(), server.LogGetPacketPoolFree());
        //wprintf(L"[ChunckPool_Job]\t> Total:%d\tUse:%d\tFree:%d\n", server.Log_GetJobPoolTotal(), server.Log_GetJobPoolUse(), server.Log_GetJobPoolFree());
        ///*wprintf(L"[ChunckPool_Packet]\t> FreePacketChunck:%d\tPoolTotal:%d\n", server.GetStackSize(), server.GetTotalPoolCnt());
        //wprintf(L"[ChunckPool_SendQ]\t> FreeLFQChunck:%d\tPoolTotal:%d\n", LockFreeQueue<Packet*>::GetStackSize(), LockFreeQueue<Packet*>::GetPool_TotalSize());
        //wprintf(L"[ChunckPool_Job]\t> FreeJobChunck:%d\tPoolTotal:%d\n\n", server.Log_JobQueue_StackSize(), server.Log_JobQueue_TotalPool());
        //wprintf(L"[CharacterPool]\t> Total:%d\tUse:%d\tFree:%d\n", server.Log_GetCharacterPool_Total(), server.Log_GetCharacterPool_Use(), server.Log_GetCharacterPool_Free());*/
        //wprintf(L"[JobQueue]\t> RemainJob:%d\tJob/s:%d\n", server.RemainJob(), server.GetJobTPS());
        //wprintf(L"[UpdateThread]\t> SleepTime/s:%d ms\tRunningTime/s:%d\n", server.GetThreadRunningTime(), server.GetThreadRunningTPS());
        //wprintf(L"====================================================================================\n");
        //monitor.UpdateAll();

        //wprintf(L"[CPU]\t\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n", monitor.ProcessorTotal(), monitor.ProcessorUser(), monitor.ProcessorKernel());
        //wprintf(L"[Process]\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n\n", monitor.ProcessTotal(), monitor.ProcessUser(), monitor.ProcessKernel());

        //wprintf(L"[Process]\t> UserAllocMemory:%u B\t\tNonPagedMemory:%d B\n", monitor.ProcessUserAllocMemory(), monitor.ProcessNonPagedMemory());
        //wprintf(L"[PC]\t\t> AvailableMemory:%d MB\t\tNonPagedMemory:%d B\n\n", monitor.AvailableMemory(), monitor.NonpagedMemory());

        //wprintf(L"[Network]\t> RecvBytes:%f B\t\tSendBytes:%f B\n", monitor.NetworkRecvBytes(), monitor.NetworkSendBytes());
        //wprintf(L"====================================================================================\n");
        //wprintf(L"\n\n\n");

        Sleep(1000);
    }


    return 0;
}