#define FD_SETSIZE 1024
#pragma comment(lib, "ws2_32")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include "WorkerThread.h"
#include "ClientSession.h"

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 12001
#define IDEAL_PER_THREAD 256

int wmain(int argc, WCHAR* argv[])
{
	// IP/Port는 args (기본값 fallback), N·MessageInterval은 콘솔 입력
	char serverIp[64] = DEFAULT_IP;
	int serverPort = DEFAULT_PORT;
	int N = 0;
	int messageInterval = 0;

	if (argc >= 2) WideCharToMultiByte(CP_ACP, 0, argv[1], -1, serverIp, sizeof(serverIp), NULL, NULL);
	if (argc >= 3) serverPort = _wtoi(argv[2]);

	if (serverPort <= 0)
	{
		wprintf(L"Usage: DummyClient.exe [IP] [Port]\n");
		wprintf(L"Default: %hs %d\n", DEFAULT_IP, DEFAULT_PORT);
		return 1;
	}

	wprintf(L"=== DummyClient v1 ===\n");
	wprintf(L"Server: %hs:%d\n\n", serverIp, serverPort);

	wprintf(L"Input Client Count : ");
	wscanf_s(L"%d", &N);
	wprintf(L"Input Message Interval (ms) : ");
	wscanf_s(L"%d", &messageInterval);

	if (N <= 0 || messageInterval <= 0)
	{
		wprintf(L"Invalid input — Client Count and Message Interval must be positive\n");
		return 1;
	}

	DWORD minLifetime = 0, maxLifetime = 0;
	wprintf(L"Input Disconnect Min Lifetime (ms, 0=disable) : ");
	wscanf_s(L"%u", &minLifetime);
	if (minLifetime > 0)
	{
		wprintf(L"Input Disconnect Max Lifetime (ms) : ");
		wscanf_s(L"%u", &maxLifetime);
		if (maxLifetime <= minLifetime)
		{
			wprintf(L"Invalid — Max must be greater than Min\n");
			return 1;
		}
	}

	// 프로세서 수 자동 감지 + 스레드/세션 분배
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	DWORD processorCount = sysInfo.dwNumberOfProcessors;

	int threadCount = (N + IDEAL_PER_THREAD - 1) / IDEAL_PER_THREAD;
	if (threadCount > (int)processorCount) threadCount = (int)processorCount;
	if (threadCount < 1) threadCount = 1;

	int basePerThread = N / threadCount;
	int remainder = N % threadCount;

	wprintf(L"\nTotal Clients: %d\n", N);
	wprintf(L"Processors: %u, Worker Threads: %d\n", processorCount, threadCount);
	wprintf(L"Per-thread: %d (+%d for first %d threads)\n", basePerThread, remainder ? 1 : 0, remainder);
	wprintf(L"Message Interval: %d ms\n", messageInterval);
	wprintf(L"Press Q to quit\n\n");

	// WSAStartup
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		wprintf(L"WSAStartup failed\n");
		return 1;
	}

	// Server addr
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons((u_short)serverPort);
	if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) != 1)
	{
		wprintf(L"Invalid IP: %hs\n", serverIp);
		WSACleanup();
		return 1;
	}

	// 세션 배열 할당
	ClientSession* sessions = (ClientSession*)calloc(N, sizeof(ClientSession));
	if (sessions == NULL)
	{
		wprintf(L"Memory alloc failed\n");
		WSACleanup();
		return 1;
	}
	for (int i = 0; i < N; i++)
	{
		sessions[i].sock = INVALID_SOCKET;
		sessions[i].state = eSTATE_IDLE;
	}

	// 공유 통계
	volatile LONG connected = 0, active = 0, failed = 0, disconnected = 0;
	volatile LONG tps_sent = 0, tps_recv = 0;
	volatile LONG64 totalSent = 0, totalRecv = 0;

	// shutdown event
	HANDLE shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// 워커 컨텍스트 + 스레드 생성
	WorkerContext* ctxArr = (WorkerContext*)calloc(threadCount, sizeof(WorkerContext));
	HANDLE* threadHandles = (HANDLE*)calloc(threadCount, sizeof(HANDLE));

	int sessionIdx = 0;
	for (int t = 0; t < threadCount; t++)
	{
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
		ctxArr[t].pConnected = &connected;
		ctxArr[t].pActive = &active;
		ctxArr[t].pFailed = &failed;
		ctxArr[t].pDisconnected = &disconnected;
		ctxArr[t].pTPS_Sent = &tps_sent;
		ctxArr[t].pTPS_Recv = &tps_recv;
		ctxArr[t].pTotalSent = &totalSent;
		ctxArr[t].pTotalRecv = &totalRecv;

		threadHandles[t] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadFunc, &ctxArr[t], 0, NULL);

		sessionIdx += thisCount;
	}

	// 통계 출력 + Q 입력 감지 루프
	DWORD startTick = GetTickCount();
	while (true)
	{
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

		wprintf(L"[%us] Conn:%d  Active:%d  Failed:%d  DC:%d  | TPS S/R: %d / %d  Total S/R: %lld / %lld bytes\n",
			elapsed, snap_connected, snap_active, snap_failed, snap_disconnected,
			snap_tps_sent, snap_tps_recv, snap_totalSent, snap_totalRecv);

		if (_kbhit())
		{
			int ch = _getch();
			if (ch == 'q' || ch == 'Q')
			{
				wprintf(L"\nShutdown requested...\n");
				SetEvent(shutdownEvent);
				break;
			}
		}
	}

	// 워커 종료 대기
	WaitForMultipleObjects(threadCount, threadHandles, TRUE, 5000);

	for (int t = 0; t < threadCount; t++) CloseHandle(threadHandles[t]);
	CloseHandle(shutdownEvent);

	free(threadHandles);
	free(ctxArr);
	free(sessions);

	WSACleanup();

	wprintf(L"Done.\n");
	return 0;
}
