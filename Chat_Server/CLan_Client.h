#pragma once
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <windows.h>

#include "CPacket.h"
#include "LockFree_Queue_TLS.h"
#include "Logger.h"


class CLan_Client
{
public:
	CLan_Client();

	bool Connect(const WCHAR* ServerIP, u_short ServerPort, u_long WorkerThreadCnt_Total, u_long WorkerThreadCnt_Run, bool Nagle);
	bool Disconnect();

	bool SendPacket(CPacket* pPacket);

	CPacket* AllocPacket(void);
	void FreePacket(CPacket* pPacket);

	////////////////
	virtual void OnEnterJoinServer(void) = 0;		// 서버와의 연결 성공 후
	virtual void OnLeaveServer(void) = 0;			// 서버와의 연결이 끊어졌을 때

	virtual void OnRecv(CPacket* pPacket) = 0;	// 하나의 패킷 수신 완료 후
	virtual void OnSend(int iSendSize) = 0;		// 패킷 송신 완료 후

	/*virtual void OnWorkerThreadBegin() = 0;
	virtual void OnWorkerThreadEnd() = 0;*/

	/*virtual void OnError(int Error_Code)*/

private:
	struct stClient
	{
		bool ClientUseFlag;
		alignas(16) LONG64 ReleaseArr[2];

		SOCKET Socket;

		OVERLAPPED SendOverlapped;
		OVERLAPPED RecvOverlapped;

		DWORD dwSendFlag;
		DWORD dwSendPacketCnt;

		LockFree_Queue_TLS<CPacket*> SendQ;
		CPacket RecvQ{ 4096 };

		CPacket* SendBuffer[1000];

		inline int IncrementClientRef() { return InterlockedIncrement64(&ReleaseArr[1]); }
		inline LONG64 DecrementClientRef() { return InterlockedDecrement64(&ReleaseArr[1]); }
	};

	static unsigned WINAPI WorkerThread(LPVOID lpThreadParameter);

	bool SendPost(DWORD Flag = 0);
	bool RecvPost(DWORD Flag = 0);

	bool CheckPacketMessageComplete(void);
	void GetPacketMessage(CPacket* pPacket);

	void SetPacketHeader(CPacket* pPacket);

	void InitClient(void);

private:
	HANDLE h_IOCP;
	stClient Client;

	HANDLE* hWorkerThread;
	u_long ThreadCnt;

	Logger* pLogger;
};