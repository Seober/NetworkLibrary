#pragma once
#include <winsock2.h>
#include <windows.h>

enum class SessionState {
    kIdle = 0,      // 초기 상태, 아직 socket 미생성
    kConnecting,    // connect() 호출 후 완료 대기 (non-blocking)
    kConnected,     // 연결 완료, 로그인 패킷 송신 대기
    kLoginSent,     // 로그인 패킷 송신 완료, 응답 대기
    kActive,        // 로그인 성공, 채팅 송신 가능
    kDisconnected,  // 연결 끊김 — reconnectAt 후 재연결 시도
};

struct ClientSession {
    SOCKET sock;
    SessionState state;

    __int64 accountNo;
    WORD sectorX;
    WORD sectorY;

    // 송신 버퍼: 인코딩된 패킷 누적, send 가능할 때 비워냄
    char sendBuf[8192];
    int sendBytes;

    // 수신 버퍼: recv 결과 누적, 헤더+페이로드 단위로 파싱
    char recvBuf[8192];
    int recvBytes;

    DWORD lastHeartbeat;  // GetTickCount 기준 마지막 하트비트 송신 시각
    DWORD lastMessage;    // 마지막 메시지 송신 시각
    DWORD disconnectAt;   // ACTIVE 진입 시 설정, 도달 시 무작위 disconnect
    DWORD reconnectAt;    // DISCONNECTED 후 재연결 시도 시각

    int messagesSent;
    int messagesRecv;
};
