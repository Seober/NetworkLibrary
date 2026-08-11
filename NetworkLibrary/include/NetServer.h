#pragma once
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Packet.h"
#include "LockFreeQueue.h"
#include "IPacketEncoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////

class NetServer {
public:
    struct Session {
        bool SessionUseFlag;
        // [0]=ReleaseFlag(FALSE=활성/TRUE=해제), [1]=RefCnt. alignas(16)+CompareExchange128로
        // 16-byte 동시 atomic 검증 (DisconnectSession 중복 차단, C-1·C-4 검토 완료).
        // 3d-2c: MSVC 16-byte std::atomic은 lock-free 미보장(핫패스 위험) → Interlocked 최적,
        // raw 유지. Linux 포팅(Phase 4) 시 추상화 재정리.
        alignas(16) volatile LONG64 ReleaseArr[2];
        unsigned __int64 SessionID;

        char LastAction;

        SOCKET Socket;
        SOCKADDR_IN ClientAddr;

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

    static unsigned WINAPI AcceptThread(LPVOID lpThreadParameter);
    static unsigned WINAPI WorkerThread(LPVOID lpThreadParameter);

    NetServer(IPacketEncoder* encoder = nullptr);
    ~NetServer();

    bool Start(const WCHAR* serverIP, u_short serverPort, u_short workerThreadCnt_Total,
               u_short workerThreadCnt_Run, BOOL nagle, u_short connectSession_Max);

    // 정상 종료 — 모든 active session에 OnClientLeave 콜백 보장 후 thread 종료.
    // idempotent (다중 호출 안전), 동기 (return 시 모든 thread 종료 완료).
    //
    // [중요] 파생 클래스는 자신의 소멸자에서 Stop()을 호출해야 한다.
    //   ~NetServer 본체가 실행될 시점에는 vtable이 NetServer로 되돌아가 있어
    //   OnClientLeave(순수 가상) 디스패치가 불가능하다 (purecall -> Crash).
    //   ~NetServer의 Stop() 호출은 thread·자원 정리를 위한 안전망일 뿐,
    //   그 시점에 살아있는 session이 있으면 콜백을 보장할 수 없다.
    void Stop();
    /*void Stop();*/
    u_long GetConnectedSessionCnt() { return TotalSessionCnt - FreeSessionStack.GetUseSize(); }
    u_long GetDisconnectedSessionCnt() { return FreeSessionStack.GetUseSize(); }
    u_long GetTotalSessionCnt() { return TotalSessionCnt; }

    Session* FindSession(unsigned __int64 sessionID) { return &SessionArr[sessionID >> 48]; }
    Session* GetFreeSession(void);

    void KillSession(unsigned __int64 sessionID);
    void DisconnectSession(Session* session);

    void SendPacket(unsigned __int64 sessionID, Packet* packet);

    virtual bool OnConnectionRequest() = 0;
    virtual void OnClientJoin(unsigned __int64 sessionID) = 0;
    virtual void OnClientLeave(unsigned __int64 sessionID) = 0;
    virtual void OnRecv(unsigned __int64 sessionID, Packet* packet) = 0;

    /*SOCKET GetListenSocket() { return ListenSocket; }*/
    unsigned __int64 GenerateSessionID() { return ++SessionIDCnt; }
    /*HANDLE GetIOCPHandle() { return IOCP; }*/

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

    void AddAcceptCnt(void) {
        AcceptTotal.fetch_add(1);
        AcceptTPS.fetch_add(1);
    }

    unsigned __int64 GetAcceptTotal(void) { return AcceptTotal.load(); }
    DWORD GetAcceptTPS(void) { return AcceptTPS.exchange(0); }

private:
    void ReleaseSession(Session* session);
    void InitSession(Session* session);

    // thread_local 캐시로 LogTransmit_Map 접근 — find/insert race 회피
    // 첫 호출 시에만 lock 잡고 map insert, 이후 호출은 lock 없이 캐시된 array 직접 접근
    std::atomic<DWORD>* GetThreadTransmitArr(void);


private:
    HANDLE IOCP;
    SOCKET ListenSocket;
    unsigned __int64 SessionIDCnt;
    std::atomic<long> Shutdown;   // Stop 트리거 플래그 (exchange로 idempotent set)

    std::unique_ptr<Session[]> SessionArr;
    u_short TotalSessionCnt;


    std::thread AcceptThread_;

    std::vector<std::thread> WorkerThreads;

    u_short ThreadCnt;

    alignas(64) std::atomic<unsigned __int64> AcceptTotal;
    std::atomic<DWORD> AcceptTPS;

    TLSChunkMemoryPool<Packet>* PacketPool;
    LockFreeStack<Session*> FreeSessionStack;

    std::shared_mutex srwLogTransmitMap;
    std::unordered_map<DWORD, std::atomic<DWORD>*>
        LogTransmit_Map;  // Value 0 : RecvTPS, Value 1 : RecvBytes, Value 2 : SendTPS, Value 3 : SendBytes

    IPacketEncoder* Encoder;
    bool OwnsEncoder;
};