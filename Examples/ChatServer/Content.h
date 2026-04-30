#pragma once
#include "CNet_Server.h"
#include "Logger.h"

#include <list>
#include <map>

#define dfSECTOR_X_MAX 50
#define dfSECTOR_Y_MAX 50

#define dfJOBTYPE_MESSAGE 0
#define dfJOBTYPE_JOIN 1
#define dfJOBTYPE_LEAVE 2
#define dfJOBTYPE_HEARTBEAT 3

	
//////////////////////////////////////////////////
//	해야할거
//	1. 빌드시키기 (제가 이것저것 지우는 바람에 빌드 바로 안될거에요, 지운건 말도안되는것만 지웠고, 간단거만 지웠어. 그리고 최소한 함수 구현 안지웠음)
//	2. 함수, 변수 명명법 통일시키기 카멜 (스네이크 다 빼셈)
//	3. 함수 이름을 그 기능에 맞게 정하기
//	4. 변수의 유효범위를 한눈에 볼 수 있게 하기. (제어권을 다른데로 던져버리지 마요.)
//	5. while true는 최대한 지양하기
//	6. 클래스 정의 함수 순서 = 구현 cpp 순서
//
//////////////////////////////////////////////////
//	생각해볼것
//	1. 자료형 통일, 이라기 보다는 플랫폼 종속성에 대해 생각해보기
//	2. 컨텐츠 / 네트워크 나누는걸 한번 생각해보기
//	3. 하나의 함수가 하나의 기능을 하는지 생각해보기, 그리고 그 대표기능이 함수의 이름에 맞는지 생각해보기
//	4. 임의의 메소드가 해당 클래스의 메소드가 맞는지 생각해보기. (디큐 잡 은 과연 서버의 기능일까? 아니면 서버가 가지고있는 잡큐 자체의 기능일까?)
//	5. 스레드 설계에 대하여, 임의의 기능을 하는 스레드를 따로 빼는것이 효율적일까? 붙이는게 효율적일까?
//	6. 함수간 기능 종속이 없도록 하기
//
//////////////////////////////////////////////////

class Chat_Server : public CNet_Server
{
public:
	struct stJob
	{
		WORD JobType;
		unsigned __int64 SessionID;
		CPacket* pPacket;
	};

	struct stCharacter
	{
		unsigned __int64 SessionID;
		INT64 AccountNo;
		short SectorX;
		short SectorY;
		DWORD dwLastRecvTime;

		WCHAR ID[20];
		WCHAR Nickname[20];
	};

public:
	Chat_Server(int MaxUser = 5000, bool HeartBeatFlag = false);
	~Chat_Server() {}



	bool OnConnectionRequest();
	void OnClientJoin(unsigned __int64 SessionID);
	void OnClientLeave(unsigned __int64 SessionID);
	void OnRecv(unsigned __int64 SessionID, CPacket* pPacket);



	void MessageControl(unsigned __int64 SessionID, CPacket* pMessagePacket);
	stCharacter* FindCharacter(unsigned __int64 SessionID);
	stCharacter* CreateCharacter(unsigned __int64 SessionID);
	void LeaveCharacter(unsigned __int64 SessionID); // 캐릭터에 대한 형태로 이름 바꾸기
	void SendPacketAround(int iSectorX, int iSectorY, CPacket* pPacket);
	void CheckHeartBeat(void);
	

public:
	//LOG
	int GetCharacterSize(void) { return CharacterMap.size(); }
	int RemainJob(void) { return JobQueue.GetUseSize(); }
	int GetThreadRunningTime(void) { return InterlockedExchange(&UpdateThreadSleepTime, 0); }
	int GetThreadRunningTPS(void) { return InterlockedExchange(&UpdateThreadRunningTPS, 0); }
	int GetJobTPS(void) { return InterlockedExchange(&_JobTPS, 0); }
	int Log_JobQueue_StackSize(void) { return JobQueue.GetStackSize(); }
	int Log_JobQueue_TotalPool(void) { return JobQueue.GetPool_TotalSize(); }
	int getGetPool_UseSize(void) { return JobQueue.GetPool_UseSize(); }
	int Log_JobQueue_FreePool(void) { return JobQueue.GetPool_FreeSize(); }
	int Log_GetCharacterPool_Total(void) { return CharacterPool.GetTotalMemCnt(); }
	int Log_GetCharacterPool_Use(void) { return CharacterPool.GetUseMemCnt(); }
	int Log_GetCharacterPool_Free(void) { return CharacterPool.GetFreeMemCnt(); }

	int Log_GetJobPoolTotal(void) { return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetTotalMemCnt(); }
	int Log_GetJobPoolUse(void) { return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetUseMemCnt(); }
	int Log_GetJobPoolFree(void) { return MemoryPool_TLS_Chunck<stJob>::GetInstance()->GetFreeMemCnt(); }

private:
	static unsigned WINAPI UpdateThread_Chat_Field1(LPVOID lpThreadParameter);
	static unsigned WINAPI TimerThread_Chat_5000(LPVOID lpThreadParameter);

	inline stJob* AllocJob(int JobType, unsigned __int64 SessionID, CPacket* pPacket);
	inline void FreeJob(stJob* pJob);

private:
	HANDLE hContentThread;
	HANDLE hTimerThread5000;
	HANDLE hJobEvent;

	unsigned int _MaxUser;
	DWORD _Running_CurTime;

	LockFree_Queue_TLS<stJob*> JobQueue;

	MemoryPool_LF<stCharacter> CharacterPool;
	std::map<unsigned __int64, stCharacter*> CharacterMap;
	std::list<stCharacter*> SectorList[dfSECTOR_Y_MAX][dfSECTOR_X_MAX];

	Logger* pLogger;


	alignas(64) long _JobTPS;
	alignas(64) long UpdateThreadRunningTPS;
	alignas(64) DWORD UpdateThreadSleepTime;
};