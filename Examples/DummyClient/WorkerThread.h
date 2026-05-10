#pragma once
#include <winsock2.h>
#include <windows.h>
#include "ClientSession.h"
#include "IPacketEncoder.h"

struct WorkerContext {
    int threadId;

    ClientSession* sessions;  // 자기 그룹 세션 배열 (포인터, 소유 아님)
    int sessionCount;
    int sessionStartIdx;  // 전역 인덱스 (AccountNo 생성용)

    SOCKADDR_IN serverAddr;
    DWORD messageInterval;  // 메시지 송신 간격 (ms)

    DWORD minLifetimeMs;  // 0 = 무작위 disconnect 비활성, 그 외 = 무작위 disconnect 최소 (ms)
    DWORD maxLifetimeMs;  // 무작위 disconnect 최대 (ms). minLifetimeMs > 0일 때만 의미

    HANDLE shutdownEvent;  // main이 SetEvent로 종료 신호

    IPacketEncoder* encoder;  // main이 생성·소유. Worker는 포인터만 사용 (stateless 공유 안전)

    // 공유 통계 (Interlocked로만 갱신)
    volatile LONG* Connected;
    volatile LONG* Active;
    volatile LONG* Failed;
    volatile LONG* Disconnected;
    volatile LONG* TPS_Sent;
    volatile LONG* TPS_Recv;
    volatile LONG64* TotalSent;
    volatile LONG64* TotalRecv;
};

unsigned __stdcall WorkerThreadFunc(LPVOID param);
