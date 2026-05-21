#pragma once
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <windows.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "Packet.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "TLSMemoryPool.h"
#include "IPacketEncoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////

class NetClient {
public:
    struct Session {
        bool SessionUseFlag;
        alignas(16) volatile LONG64 ReleaseArr[2];
        unsigned __int64 SessionID;

        SOCKET Socket;
        SOCKADDR_IN ServerAddr;

        OVERLAPPED SendOverlapped;
        OVERLAPPED RecvOverlapped;

        std::atomic<DWORD> SendFlag;
        DWORD SendPacketCnt;

        LockFreeQueue<Packet*> SendQ;
        Packet RecvQ{4096};

        Packet* SendBuffer[1000];

        inline LONG64 IncrementSessionRef() { return InterlockedIncrement64(&ReleaseArr[1]); }
        inline LONG64 DecrementSessionRef() { return InterlockedDecrement64(&ReleaseArr[1]); }
    };

    static unsigned WINAPI WorkerThread(LPVOID lpThreadParameter);

    NetClient(IPacketEncoder* encoder = nullptr);
    ~NetClient();

    bool Start(u_short workerThreadCntTotal, u_short workerThreadCntRun, u_short maxSessionCnt);

    // 정상 종료 — 모든 active session에 OnDisconnect 콜백 보장 후 thread 종료.
    // idempotent (다중 호출 안전), 동기 (return 시 모든 thread 종료 완료).
    // 소멸자에서 자동 호출됨 — 사용자가 명시적 호출 안 해도 안전.
    void Stop();

    // 동기 Connect — 성공 시 sessionID 반환, 실패 시 0
    // OnConnect 콜백은 caller thread에서 호출됨 (caller thread block 회피 위해 가벼운 작업만 권장)
    unsigned __int64 Connect(const WCHAR* serverIP, u_short serverPort);

    u_long GetConnectedSessionCnt() { return TotalSessionCnt - FreeSessionStack.GetUseSize(); }
    u_long GetDisconnectedSessionCnt() { return FreeSessionStack.GetUseSize(); }
    u_long GetTotalSessionCnt() { return TotalSessionCnt; }

    Session* FindSession(unsigned __int64 sessionID) { return &SessionArr[sessionID >> 48]; }
    Session* GetFreeSession(void);

    void KillSession(unsigned __int64 sessionID);
    void Disconnect(unsigned __int64 sessionID);   // 사용자 요청 disconnect (KillSession wrapper)
    void DisconnectSession(Session* session);

    void SendPacket(unsigned __int64 sessionID, Packet* packet);

    virtual void OnConnect(unsigned __int64 sessionID) = 0;
    virtual void OnDisconnect(unsigned __int64 sessionID) = 0;
    virtual void OnRecv(unsigned __int64 sessionID, Packet* packet) = 0;

    unsigned __int64 GenerateSessionID() { return ++SessionIDCnt; }

    bool SendPost(Session* session, DWORD flag = 0);
    bool RecvPost(Session* session, DWORD flag = 0);

    Packet* AllocPacket(void);
    void FreePacket(Packet* packet);

    bool CheckPacketMessageComplete(unsigned __int64 sessionID, Packet* recvQ);
    bool GetPacketMessage(Packet* packet, Packet* recvQ);

    ////////////////////////////////////////////////////////////////////////////////
    int LogGetPacketPoolTotal(void) { return PacketPool->GetTotalMemCnt(); }
    int LogGetPacketPoolUse(void) { return PacketPool->GetUseMemCnt(); }
    int LogGetPacketPoolFree(void) { return PacketPool->GetFreeMemCnt(); }

    void AddRecvBytes(DWORD recvBytes);
    void AddRecvPacket(void);
    void AddSend(DWORD sendPacketCnt, DWORD sendBytes);

    void GetTransmit(DWORD* transmitBuffer);

private:
    void ReleaseSession(Session* session);
    void InitSession(Session* session);

    // thread_local 캐시로 LogTransmit_Map 접근 — find/insert race 회피
    // 첫 호출 시에만 lock 잡고 map insert, 이후 호출은 lock 없이 캐시된 array 직접 접근
    std::atomic<DWORD>* GetThreadTransmitArr(void);


private:
    HANDLE IOCP;
    unsigned __int64 SessionIDCnt;
    std::atomic<long> Shutdown;   // Stop 트리거 플래그 (exchange로 idempotent set)

    Session* SessionArr;
    u_short TotalSessionCnt;

    DWORD* WorkerThreadID;
    HANDLE* WorkerThread_;

    u_short ThreadCnt;

    TLSChunkMemoryPool<Packet>* PacketPool;
    LockFreeStack<Session*> FreeSessionStack;

    std::shared_mutex srwLogTransmitMap;
    std::unordered_map<DWORD, std::atomic<DWORD>*>
        LogTransmit_Map;  // Value 0 : RecvTPS, Value 1 : RecvBytes, Value 2 : SendTPS, Value 3 : SendBytes

    IPacketEncoder* Encoder;
    bool OwnsEncoder;
};
