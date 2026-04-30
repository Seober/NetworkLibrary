#pragma once
#include <winsock2.h>
#include <windows.h>

enum eSESSION_STATE
{
	eSTATE_IDLE = 0,        // 초기 상태, 아직 socket 미생성
	eSTATE_CONNECTING,      // connect() 호출 후 완료 대기 (non-blocking)
	eSTATE_CONNECTED,       // 연결 완료, 로그인 패킷 송신 대기
	eSTATE_LOGIN_SENT,      // 로그인 패킷 송신 완료, 응답 대기
	eSTATE_ACTIVE,          // 로그인 성공, 채팅 송신 가능
	eSTATE_DISCONNECTED,    // 연결 끊김 (재연결 안 함)
	eSTATE_FAILED,          // 연결/로그인 실패
};

struct ClientSession
{
	SOCKET sock;
	eSESSION_STATE state;

	__int64 accountNo;
	WORD sectorX;
	WORD sectorY;

	// 송신 버퍼: 인코딩된 패킷 누적, send 가능할 때 비워냄
	char sendBuf[8192];
	int sendBytes;

	// 수신 버퍼: recv 결과 누적, 헤더+페이로드 단위로 파싱
	char recvBuf[8192];
	int recvBytes;

	DWORD lastHeartbeat;    // GetTickCount 기준 마지막 하트비트 송신 시각
	DWORD lastMessage;      // 마지막 메시지 송신 시각

	int messagesSent;
	int messagesRecv;
};
