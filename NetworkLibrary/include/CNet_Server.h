#pragma once
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <windows.h>

#include <unordered_map>

#include "CPacket.h"
#include "LockFree_Queue_TLS.h"

////////////////////////////////////////////////////////////////////////////////////////////////

class CNet_Server
{
public:
	struct stSESSION
	{
		bool SessionUseFlag;
		alignas(16) volatile LONG64 ReleaseArr[2];
		unsigned __int64 SessionID;

		char LastAction;

		SOCKET Socket;
		SOCKADDR_IN ClientAddr;

		OVERLAPPED SendOverlapped;
		OVERLAPPED RecvOverlapped;

		volatile DWORD dwSendFlag;
		DWORD dwSendPacketCnt;

		LockFree_Queue_TLS<CPacket*> SendQ;
		CPacket RecvQ{ 4096 };

		CPacket* SendBuffer[1000];

		inline LONG64 IncrementSessionRef() { return InterlockedIncrement64(&ReleaseArr[1]); }
		inline LONG64 DecrementSessionRef() { return InterlockedDecrement64(&ReleaseArr[1]); }
	};

	static unsigned WINAPI AcceptThread(LPVOID lpThreadParameter);
	static unsigned WINAPI WorkerThread(LPVOID lpThreadParameter);

	CNet_Server();
	~CNet_Server();

	bool Start(const WCHAR* ServerIP, u_short ServerPort, u_short WorkerThreadCnt_Total, u_short WorkerThreadCnt_Run, BOOL Nagle, u_short ConnectSession_Max);
	/*void Stop();*/
	u_long GetSessionCnt_Connected() { return SessionCnt_Total - FreeSessionStack.GetUseSize(); }
	u_long GetSessionCnt_Disconnected() { return FreeSessionStack.GetUseSize(); }
	u_long GetSessionCnt_Total() { return SessionCnt_Total; }

	stSESSION* FindSession(unsigned __int64 SessionID) { return &SessionArr[SessionID >> 48]; }
	stSESSION* GetFreeSession(void);

	void KillSession(unsigned __int64 SessionID);
	void DisconnectSession(stSESSION* pSession);

	void SendPacket(unsigned __int64 SessionID, CPacket* pPacket);

	virtual bool OnConnectionRequest() = 0;
	virtual void OnClientJoin(unsigned __int64 SessionID) = 0;
	virtual void OnClientLeave(unsigned __int64 SessionID) = 0;
	virtual void OnRecv(unsigned __int64 SesisonID, CPacket* pPacket) = 0;

	/*SOCKET GetListenSocket() { return ListenSocket; }*/
	unsigned __int64 GetSessionID_New() { return ++SessionID_Cnt; }
	/*HANDLE GetIOCPHandle() { return h_IOCP; }*/

	bool SendPost(stSESSION* pSession, DWORD Flag = 0);
	bool RecvPost(stSESSION* pSession, DWORD Flag = 0);

	CPacket* AllocPacket(void);
	void FreePacket(CPacket* pPacket);

	bool CheckPacketMessageComplete(unsigned __int64 SessionID, CPacket* pRecvQ);
	bool GetPacketMessage(CPacket* pPacket, CPacket* pRecvQ);

	////////////////////////////////////////////////////////////////////////////////
	int Log_GetPacketPoolTotal(void) { return pPacketPool->GetTotalMemCnt(); }
	int Log_GetPacketPoolUse(void) { return pPacketPool->GetUseMemCnt(); }
	int Log_GetPacketPoolFree(void) { return pPacketPool->GetFreeMemCnt(); }

	int GetStackSize(void) { return pPacketPool->GetStackSize(); }
	int GetPoolCnt_Total(void) { return pPacketPool->GetPoolCnt_Total(); }
	int GetPoolCnt_Use(void) { return pPacketPool->GetPoolCnt_Use(); }
	int GetPoolCnt_Free(void) { return pPacketPool->GetPoolCnt_Free(); }

	void AddRecvBytes(DWORD dwRecvBytes);
	void AddRecvPacket(void);
	void AddSend(DWORD dwSendPacketCnt, DWORD dwSendBytes);

	void GetTransmit(DWORD* TransmitBuffer);

	void AddAcceptCnt(void)
	{
		InterlockedExchangeAdd(&_AcceptTotal, 1);
		InterlockedExchangeAdd(&_AcceptTPS, 1);
	}

	unsigned __int64 GetAcceptTotal(void) { return _AcceptTotal; }
	DWORD GetAcceptTPS(void) { return InterlockedExchange(&_AcceptTPS, 0); }

private:
	void SetPacketHeader(CPacket* pPacket);
	void EncodePacket(CPacket* pPacket);

	bool DecodePacket(CPacket* pPacket);


	void ReleaseSession(stSESSION* pSession);
	void initSession(stSESSION* pSession);


private:
	enum en_NETWORK_PACKET
	{
		eHEADER_CODE = 0x77,
		eHEADER_KEY = 0x32
	};

#pragma pack(push, 1)
	struct stHeader_NET
	{
		BYTE Code;
		WORD Len;
		BYTE RKey;
		BYTE Checksum;
	};
#pragma pack(pop)

private:
	HANDLE h_IOCP;
	SOCKET ListenSocket;
	unsigned __int64 SessionID_Cnt;

	stSESSION* SessionArr;
	u_short SessionCnt_Total;


	DWORD dwAcceptThreadID;
	HANDLE hAcceptThread;

	DWORD* dwWorkerThreadID;
	HANDLE* hWorkerThread;

	u_short ThreadCnt;

	alignas(64) unsigned __int64 _AcceptTotal;
	DWORD _AcceptTPS;

	MemoryPool_TLS_Chunck<CPacket>* pPacketPool;
	LockFree_Stack<stSESSION*> FreeSessionStack;

	SRWLOCK srwLogTransmitMap;
	std::unordered_map<DWORD, DWORD*> _LogTransmit_Map; // Value 0 : RecvTPS, Value 1 : RecvBytes, Value 2 : SendTPS, Value 3 : SendBytes
};