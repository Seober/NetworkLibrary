//#include "CNet_Server.h"

#include <stdio.h>
#include <conio.h>
#include <time.h>

//#include "CNet_Server.h"
#include "Content.h";

#include "Logger.h"
#include "Monitoring_Tool.h"

#include "MonitorClient.h"


#define SERVER_IP NULL
#define SERVER_PORT 12001

#define MAX_USER 16000

#define ERR_MAKE Logger::GetInstance()->Crash();

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void)
{
	Logger* pLoger = Logger::GetInstance();
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);

	int ProcessorCnt = SystemInfo.dwNumberOfProcessors - 1;

	BOOL NagleFlag;

	wprintf(L"Input NagleFlag[0:OFF, 1:ON] : ");
	scanf_s("%d", &NagleFlag);
	if (NagleFlag) NagleFlag = FALSE;
	else NagleFlag = TRUE;

	BOOL MonitorFlag;
	wprintf(L"Use Monitor[0:OFF, 1:ON] : ");
	scanf_s("%d", &MonitorFlag);

	Chat_Server Server(MAX_USER);
	/*Server.CreateContentThread(true);*/
	if (!Server.Start(SERVER_IP, SERVER_PORT, ProcessorCnt, 0, NagleFlag, MAX_USER * 2)) return 0;

	MonitorClient MonitorClient(1);
	if (MonitorFlag)
	{
		if (MonitorClient.Connect(L"192.168.10.101", 10101, 2, 0, true) == false) return 0;
	}

	DWORD dwMainThreadID = GetCurrentThreadId();
	WCHAR ControlKey;

	
	u_int64 LoopCnt = 0;
	Monitoring_Tool Monitor;

	DWORD TransmitBuffer[4] = { 0,};
	int ProcessUseCPUTotal;
	int ProcessUseMemory;
	
	int SessionCnt;
	int UserCnt;
	int JobTPS;
	int UsePacketPool;
	int UseJobPool;

	int ProcessorUseCPUTotal;
	int ProcessorNonPagedMemory;
	int ProcessorNetworkRecv;
	int ProcessorNetworkSend;
	int ProcessorAvailableMemory;

	int CurTime;

	while (1)
	{

		if (_kbhit())
		{
			ControlKey = _getwch();
			if (ControlKey == L'q' || ControlKey == L'Q') break;
		}

		Server.GetTransmit(TransmitBuffer);
		Monitor.UpdateAll();

		ProcessUseCPUTotal = Monitor.ProcessTotal();
		ProcessUseMemory = Monitor.ProcessUserAllocMemory() / 1000000;

		SessionCnt = Server.GetSessionCnt_Connected();
		UserCnt = Server.GetCharacterSize();
		JobTPS = Server.GetJobTPS();
		UsePacketPool = Server.Log_GetPacketPoolUse();
		UseJobPool = Server.Log_GetJobPoolUse();
		
		ProcessorUseCPUTotal = Monitor.ProcessorTotal();
		ProcessorNonPagedMemory = Monitor.NonpagedMemory() / 1000000;
		ProcessorNetworkRecv = Monitor.NetworkRecvBytes() / 1000;
		ProcessorNetworkSend = Monitor.NetworkSendBytes() / 1000;
		ProcessorAvailableMemory = Monitor.AvailableMemory();

		time((time_t*)&CurTime);
		if (MonitorFlag)
		{
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1, CurTime);

			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, ProcessUseCPUTotal, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, ProcessUseMemory, CurTime);

			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_SESSION, SessionCnt, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_PLAYER, UserCnt, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, JobTPS, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, UsePacketPool, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, UseJobPool, CurTime);

			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL, ProcessorUseCPUTotal, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY, ProcessorNonPagedMemory, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV, ProcessorNetworkRecv, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND, ProcessorNetworkSend, CurTime);
			MonitorClient.SendLogMessage(dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY, ProcessorAvailableMemory, CurTime);
		}

		wprintf(L"\n====================================[LoopCnt:%I64u]=====================================\n", LoopCnt++);
		wprintf(L"SessionCnt:%d, CharacterCnt:%d\n\n", SessionCnt, UserCnt);
		wprintf(L"[Network_Logic]\t> RecvTPS:%d, RecvBytes:%d kB\n", TransmitBuffer[0], TransmitBuffer[1] / 1000);
		wprintf(L"[Network_Logic]\t> SendTPS:%d, SendBytes:%d kB\n", TransmitBuffer[2], TransmitBuffer[3] / 1000);
		wprintf(L"[Network_Logic]\t> AcceptTotal:%I64u\t\tAcceptTPS:%d\n\n", Server.GetAcceptTotal(), Server.GetAcceptTPS());
		wprintf(L"[ChunckPool_Packet]\t> Total:%d\tUse:%d\t\tFree:%d\n", Server.Log_GetPacketPoolTotal(), Server.Log_GetPacketPoolUse(), Server.Log_GetPacketPoolFree());
		wprintf(L"[ChunckPool_Job]\t> Total:%d\tUse:%d\t\tFree:%d\n\n", Server.Log_GetJobPoolTotal(), Server.Log_GetJobPoolUse(), Server.Log_GetJobPoolFree());
		wprintf(L"[JobQueue]\t> RemainJob:%d\tJob/s:%d\n", Server.RemainJob(), JobTPS);
		wprintf(L"[UpdateThread]\t> SleepTime/s:%d ms\tRunningTime/s:%d\n", Server.GetThreadRunningTime(), Server.GetThreadRunningTPS());
		wprintf(L"====================================================================================\n");

		wprintf(L"[CPU]\t\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n", Monitor.ProcessorTotal(), Monitor.ProcessorUser(), Monitor.ProcessorKernel());
		wprintf(L"[Process]\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n\n", Monitor.ProcessTotal(), Monitor.ProcessUser(), Monitor.ProcessKernel());

		wprintf(L"[Process]\t> UserAllocMemory:%u MB\t\tNonPagedMemory:%d MB\n", ProcessUseMemory, Monitor.ProcessNonPagedMemory() / 1000000);
		wprintf(L"[PC]\t\t> AvailableMemory:%d MB\t\tNonPagedMemory:%d MB\n\n", ProcessorAvailableMemory, ProcessorNonPagedMemory);

		wprintf(L"[Network]\t> RecvBytes:%d kB\t\tSendBytes:%d kB\n", ProcessorNetworkRecv, ProcessorNetworkSend);
		wprintf(L"====================================================================================\n");
		wprintf(L"\n\n\n");




		//Server.GetTransmit(TransmitBuffer);

		//wprintf(L"\n====================================[LoopCnt:%I64u]=====================================\n", LoopCnt++);
		//wprintf(L"SessionCnt:%d, CharacterCnt:%d\n\n", Server.GetSessionCnt_Connected(), Server.GetCharacterSize());
		//wprintf(L"[Network_Logic]\t> RecvTPS:%d, RecvBytes:%d B\n", TransmitBuffer[0], TransmitBuffer[1]);
		//wprintf(L"[Network_Logic]\t> SendTPS:%d, SendBytes:%d B\n", TransmitBuffer[2], TransmitBuffer[3]);
		//wprintf(L"[Network_Logic]\t> AcceptTotal:%I64u\t\tAcceptTPS:%d\n\n", Server.GetAcceptTotal(), Server.GetAcceptTPS());
		//wprintf(L"[ChunckPool_Packet]\t> Total:%d\tUse:%d\tFree:%d\n", Server.Log_GetPacketPoolTotal(), Server.Log_GetPacketPoolUse(), Server.Log_GetPacketPoolFree());
		//wprintf(L"[ChunckPool_Job]\t> Total:%d\tUse:%d\tFree:%d\n", Server.Log_GetJobPoolTotal(), Server.Log_GetJobPoolUse(), Server.Log_GetJobPoolFree());
		///*wprintf(L"[ChunckPool_Packet]\t> FreePacketChunck:%d\tPoolTotal:%d\n", Server.GetStackSize(), Server.GetPoolCnt_Total());
		//wprintf(L"[ChunckPool_SendQ]\t> FreeLFQChunck:%d\tPoolTotal:%d\n", LockFree_Queue_TLS<CPacket*>::GetStackSize(), LockFree_Queue_TLS<CPacket*>::GetPool_TotalSize());
		//wprintf(L"[ChunckPool_Job]\t> FreeJobChunck:%d\tPoolTotal:%d\n\n", Server.Log_JobQueue_StackSize(), Server.Log_JobQueue_TotalPool());
		//wprintf(L"[CharacterPool]\t> Total:%d\tUse:%d\tFree:%d\n", Server.Log_GetCharacterPool_Total(), Server.Log_GetCharacterPool_Use(), Server.Log_GetCharacterPool_Free());*/
		//wprintf(L"[JobQueue]\t> RemainJob:%d\tJob/s:%d\n", Server.RemainJob(), Server.GetJobTPS());
		//wprintf(L"[UpdateThread]\t> SleepTime/s:%d ms\tRunningTime/s:%d\n", Server.GetThreadRunningTime(), Server.GetThreadRunningTPS());
		//wprintf(L"====================================================================================\n");
		//Monitor.UpdateAll();

		//wprintf(L"[CPU]\t\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n", Monitor.ProcessorTotal(), Monitor.ProcessorUser(), Monitor.ProcessorKernel());
		//wprintf(L"[Process]\t> Total:%f%%\tUser:%f%%\t\tKernel:%f%%\n\n", Monitor.ProcessTotal(), Monitor.ProcessUser(), Monitor.ProcessKernel());

		//wprintf(L"[Process]\t> UserAllocMemory:%u B\t\tNonPagedMemory:%d B\n", Monitor.ProcessUserAllocMemory(), Monitor.ProcessNonPagedMemory());
		//wprintf(L"[PC]\t\t> AvailableMemory:%d MB\t\tNonPagedMemory:%d B\n\n", Monitor.AvailableMemory(), Monitor.NonpagedMemory());

		//wprintf(L"[Network]\t> RecvBytes:%f B\t\tSendBytes:%f B\n", Monitor.NetworkRecvBytes(), Monitor.NetworkSendBytes());
		//wprintf(L"====================================================================================\n");
		//wprintf(L"\n\n\n");

		Sleep(1000);
	}


	return 0;
}