#pragma once
#include "CNet_Server.h"
#include "Logger.h"

#include <list>
#include <map>

class Chat_Server : public CNet_Server {
public:
    static constexpr int kSectorXMax = 50;
    static constexpr int kSectorYMax = 50;

    enum class JobType : WORD {
        kMessage = 0,
        kJoin,
        kLeave,
        kHeartbeat,
    };

    struct stJob {
        JobType type;
        unsigned __int64 SessionID;
        CPacket* Packet;
    };

    struct stCharacter {
        unsigned __int64 SessionID;
        INT64 AccountNo;
        short SectorX;
        short SectorY;
        DWORD LastRecvTime;

        WCHAR ID[20];
        WCHAR Nickname[20];
    };

public:
    Chat_Server(int maxUser = 5000, bool heartBeatFlag = false);
    ~Chat_Server() {}


    bool OnConnectionRequest();
    void OnClientJoin(unsigned __int64 sessionID);
    void OnClientLeave(unsigned __int64 sessionID);
    void OnRecv(unsigned __int64 sessionID, CPacket* packet);


    void MessageControl(unsigned __int64 sessionID, CPacket* messagePacket);
    stCharacter* FindCharacter(unsigned __int64 sessionID);
    stCharacter* CreateCharacter(unsigned __int64 sessionID);
    void LeaveCharacter(unsigned __int64 sessionID);  // 캐릭터에 대한 형태로 이름 바꾸기
    void SendPacketAround(int sectorX, int sectorY, CPacket* packet);
    void CheckHeartBeat(void);


public:
    //LOG
    int GetCharacterSize(void) { return (int)CharacterMap.size(); }
    int RemainJob(void) { return JobQueue.GetUseSize(); }
    int GetThreadRunningTime(void) { return InterlockedExchange(&UpdateThreadSleepTime, 0); }
    int GetThreadRunningTPS(void) { return InterlockedExchange(&UpdateThreadRunningTPS, 0); }
    int GetJobTPS(void) { return InterlockedExchange(&JobTPS, 0); }
    int Log_JobQueue_StackSize(void) { return JobQueue.GetStackSize(); }
    int Log_JobQueue_TotalPool(void) { return JobQueue.GetPool_TotalSize(); }
    int getGetPool_UseSize(void) { return JobQueue.GetPool_UseSize(); }
    int Log_JobQueue_FreePool(void) { return JobQueue.GetPool_FreeSize(); }
    int Log_GetCharacterPool_Total(void) { return CharacterPool.GetTotalMemCnt(); }
    int Log_GetCharacterPool_Use(void) { return CharacterPool.GetUseMemCnt(); }
    int Log_GetCharacterPool_Free(void) { return CharacterPool.GetFreeMemCnt(); }

    int Log_GetJobPoolTotal(void) {
        return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTotalMemCnt();
    }
    int Log_GetJobPoolUse(void) {
        return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetUseMemCnt();
    }
    int Log_GetJobPoolFree(void) {
        return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetFreeMemCnt();
    }

private:
    static unsigned WINAPI UpdateThread_Chat_Field1(LPVOID lpThreadParameter);
    static unsigned WINAPI TimerThread_Chat_5000(LPVOID lpThreadParameter);

    inline stJob* AllocJob(JobType type, unsigned __int64 sessionID, CPacket* packet);
    inline void FreeJob(stJob* job);

private:
    HANDLE ContentThread;
    HANDLE TimerThread5000;
    HANDLE JobEvent;

    unsigned int MaxUser;
    DWORD Running_CurTime;

    LockFree_Queue_TLS<stJob*> JobQueue;

    MemoryPool_LF<stCharacter> CharacterPool;
    std::map<unsigned __int64, stCharacter*> CharacterMap;
    std::list<stCharacter*> SectorList[kSectorYMax][kSectorXMax];

    Logger* pLogger;


    alignas(64) long JobTPS;
    alignas(64) long UpdateThreadRunningTPS;
    alignas(64) DWORD UpdateThreadSleepTime;
};