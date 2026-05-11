#pragma once
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <windows.h>

#include <unordered_map>

#include "CPacket.h"
#include "LockFreeQueue.h"
#include "IPacketEncoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////

class CNet_Server {
public:
    struct stSESSION {
        bool SessionUseFlag;
        alignas(16) volatile LONG64 ReleaseArr[2];
        unsigned __int64 SessionID;

        char LastAction;

        SOCKET Socket;
        SOCKADDR_IN ClientAddr;

        OVERLAPPED SendOverlapped;
        OVERLAPPED RecvOverlapped;

        volatile DWORD SendFlag;
        DWORD SendPacketCnt;

        LockFreeQueue<CPacket*> SendQ;
        CPacket RecvQ{4096};

        CPacket* SendBuffer[1000];

        inline LONG64 IncrementSessionRef() { return InterlockedIncrement64(&ReleaseArr[1]); }
        inline LONG64 DecrementSessionRef() { return InterlockedDecrement64(&ReleaseArr[1]); }
    };

    static unsigned WINAPI AcceptThread(LPVOID lpThreadParameter);
    static unsigned WINAPI WorkerThread(LPVOID lpThreadParameter);

    CNet_Server(IPacketEncoder* encoder = nullptr);
    ~CNet_Server();

    bool Start(const WCHAR* serverIP, u_short serverPort, u_short workerThreadCnt_Total,
               u_short workerThreadCnt_Run, BOOL nagle, u_short connectSession_Max);
    /*void Stop();*/
    u_long GetSessionCnt_Connected() { return SessionCnt_Total - FreeSessionStack.GetUseSize(); }
    u_long GetSessionCnt_Disconnected() { return FreeSessionStack.GetUseSize(); }
    u_long GetSessionCnt_Total() { return SessionCnt_Total; }

    stSESSION* FindSession(unsigned __int64 sessionID) { return &SessionArr[sessionID >> 48]; }
    stSESSION* GetFreeSession(void);

    void KillSession(unsigned __int64 sessionID);
    void DisconnectSession(stSESSION* session);

    void SendPacket(unsigned __int64 sessionID, CPacket* packet);

    virtual bool OnConnectionRequest() = 0;
    virtual void OnClientJoin(unsigned __int64 sessionID) = 0;
    virtual void OnClientLeave(unsigned __int64 sessionID) = 0;
    virtual void OnRecv(unsigned __int64 sessionID, CPacket* packet) = 0;

    /*SOCKET GetListenSocket() { return ListenSocket; }*/
    unsigned __int64 GetSessionID_New() { return ++SessionID_Cnt; }
    /*HANDLE GetIOCPHandle() { return IOCP; }*/

    bool SendPost(stSESSION* session, DWORD flag = 0);
    bool RecvPost(stSESSION* session, DWORD flag = 0);

    CPacket* AllocPacket(void);
    void FreePacket(CPacket* packet);

    bool CheckPacketMessageComplete(unsigned __int64 sessionID, CPacket* recvQ);
    bool GetPacketMessage(CPacket* packet, CPacket* recvQ);

    ////////////////////////////////////////////////////////////////////////////////
    int Log_GetPacketPoolTotal(void) { return PacketPool->GetTotalMemCnt(); }
    int Log_GetPacketPoolUse(void) { return PacketPool->GetUseMemCnt(); }
    int Log_GetPacketPoolFree(void) { return PacketPool->GetFreeMemCnt(); }

    int GetStackSize(void) { return PacketPool->GetStackSize(); }
    int GetPoolCnt_Total(void) { return PacketPool->GetPoolCnt_Total(); }
    int GetPoolCnt_Use(void) { return PacketPool->GetPoolCnt_Use(); }
    int GetPoolCnt_Free(void) { return PacketPool->GetPoolCnt_Free(); }

    void AddRecvBytes(DWORD recvBytes);
    void AddRecvPacket(void);
    void AddSend(DWORD sendPacketCnt, DWORD sendBytes);

    void GetTransmit(DWORD* transmitBuffer);

    void AddAcceptCnt(void) {
        InterlockedExchangeAdd(&AcceptTotal, 1);
        InterlockedExchangeAdd(&AcceptTPS, 1);
    }

    unsigned __int64 GetAcceptTotal(void) { return AcceptTotal; }
    DWORD GetAcceptTPS(void) { return InterlockedExchange(&AcceptTPS, 0); }

private:
    void ReleaseSession(stSESSION* session);
    void initSession(stSESSION* session);

    // thread_local 캐시로 LogTransmit_Map 접근 — find/insert race 회피
    // 첫 호출 시에만 lock 잡고 map insert, 이후 호출은 lock 없이 캐시된 array 직접 접근
    DWORD* GetThreadTransmitArr(void);


private:
    HANDLE IOCP;
    SOCKET ListenSocket;
    unsigned __int64 SessionID_Cnt;

    stSESSION* SessionArr;
    u_short SessionCnt_Total;


    DWORD AcceptThreadID;
    HANDLE AcceptThread_;

    DWORD* WorkerThreadID;
    HANDLE* WorkerThread_;

    u_short ThreadCnt;

    alignas(64) unsigned __int64 AcceptTotal;
    DWORD AcceptTPS;

    TLSChunkMemoryPool<CPacket>* PacketPool;
    LockFreeStack<stSESSION*> FreeSessionStack;

    SRWLOCK srwLogTransmitMap;
    std::unordered_map<DWORD, DWORD*>
        LogTransmit_Map;  // Value 0 : RecvTPS, Value 1 : RecvBytes, Value 2 : SendTPS, Value 3 : SendBytes

    IPacketEncoder* Encoder;
    bool OwnsEncoder;
};