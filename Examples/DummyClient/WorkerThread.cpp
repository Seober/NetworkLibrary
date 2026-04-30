// FD_SETSIZE 오버라이드 — 기본 64에서 1024로 확장
// (winsock2.h가 이를 보고 fd_set 크기 결정)
#define FD_SETSIZE 1024

#include "WorkerThread.h"
#include "PacketCodec.h"
#include "../ChatServer/Protocol.h"
#include "CPacket.h"

#include <process.h>
#include <stdio.h>
#include <string.h>

#define HEARTBEAT_INTERVAL_MS 30000
#define SELECT_TIMEOUT_MS 50

static void SendLoginPacket(ClientSession& s, int globalIdx)
{
	CPacket packet;
	packet.Clear();

	WORD type = en_PACKET_CS_CHAT_REQ_LOGIN;
	__int64 accountNo = (__int64)globalIdx + 1;

	WCHAR id[20] = { 0 };
	WCHAR nickname[20] = { 0 };
	swprintf_s(id, 20, L"DC%lld", accountNo);
	swprintf_s(nickname, 20, L"DC%lld", accountNo);

	char sessionKey[64] = { 0 };

	packet << type;
	packet << accountNo;
	packet.PutData(id, 20);
	packet.PutData(nickname, 20);
	packet.PutData(sessionKey, 64);

	PacketCodec::Encode(&packet);

	int totalLen = packet.GetDataSize();
	int copyLen = totalLen;
	if (s.sendBytes + copyLen > sizeof(s.sendBuf)) return;

	memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
	s.sendBytes += copyLen;

	s.accountNo = accountNo;
	s.state = eSTATE_LOGIN_SENT;
}

static void SendSectorMovePacket(ClientSession& s)
{
	CPacket packet;
	packet.Clear();

	WORD type = en_PACKET_CS_CHAT_REQ_SECTOR_MOVE;
	WORD sectorX = (WORD)(rand() % 50);
	WORD sectorY = (WORD)(rand() % 50);

	packet << type;
	packet << s.accountNo;
	packet << sectorX;
	packet << sectorY;

	PacketCodec::Encode(&packet);

	int copyLen = packet.GetDataSize();
	if (s.sendBytes + copyLen > sizeof(s.sendBuf)) return;

	memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
	s.sendBytes += copyLen;

	s.sectorX = sectorX;
	s.sectorY = sectorY;
}

static void SendChatPacket(ClientSession& s)
{
	CPacket packet;
	packet.Clear();

	WORD type = en_PACKET_CS_CHAT_REQ_MESSAGE;
	WCHAR message[64];
	int len = swprintf_s(message, 64, L"Test from DC%lld", s.accountNo);
	WORD messageLen = (WORD)(len * sizeof(WCHAR));

	packet << type;
	packet << s.accountNo;
	packet << messageLen;
	packet.PutData(message, len);

	PacketCodec::Encode(&packet);

	int copyLen = packet.GetDataSize();
	if (s.sendBytes + copyLen > sizeof(s.sendBuf)) return;

	memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
	s.sendBytes += copyLen;

	s.messagesSent++;
}

static void SendHeartbeatPacket(ClientSession& s)
{
	CPacket packet;
	packet.Clear();

	WORD type = en_PACKET_CS_CHAT_REQ_HEARTBEAT;
	packet << type;

	PacketCodec::Encode(&packet);

	int copyLen = packet.GetDataSize();
	if (s.sendBytes + copyLen > sizeof(s.sendBuf)) return;

	memcpy(s.sendBuf + s.sendBytes, packet.GetReadBufferPtr(), copyLen);
	s.sendBytes += copyLen;
}

// recvBuf에 누적된 데이터에서 완성된 패킷을 모두 처리
// 반환: 처리된 바이트 수
static int ProcessRecvBuffer(ClientSession& s, WorkerContext* ctx)
{
	int processed = 0;
	int headerSize = sizeof(PacketCodec::stHeader_NET);

	while (s.recvBytes - processed >= headerSize)
	{
		PacketCodec::stHeader_NET* pHeader = (PacketCodec::stHeader_NET*)(s.recvBuf + processed);

		if (pHeader->Code != PacketCodec::eHEADER_CODE)
		{
			// 잘못된 패킷 — 세션 종료
			s.state = eSTATE_DISCONNECTED;
			return processed;
		}

		if (pHeader->Len > sizeof(s.recvBuf) - headerSize)
		{
			s.state = eSTATE_DISCONNECTED;
			return processed;
		}

		int packetSize = headerSize + pHeader->Len;
		if (s.recvBytes - processed < packetSize) break;  // 페이로드 부족

		// 임시 CPacket으로 복호화
		CPacket tmpPacket;
		memcpy(tmpPacket.GetWriteBufferPtr(), s.recvBuf + processed, packetSize);
		tmpPacket.MoveWritePos(packetSize);

		if (!PacketCodec::Decode(&tmpPacket))
		{
			// checksum 실패 — 세션 종료
			s.state = eSTATE_DISCONNECTED;
			return processed;
		}

		// 패킷 타입 분기
		WORD type;
		tmpPacket >> type;

		switch (type)
		{
		case en_PACKET_CS_CHAT_RES_LOGIN:
		{
			BYTE status;
			__int64 accountNo;
			tmpPacket >> status >> accountNo;
			if (status == 1)
			{
				s.state = eSTATE_ACTIVE;
				InterlockedIncrement(ctx->pActive);
				// 로그인 성공 즉시 섹터 이동
				SendSectorMovePacket(s);
			}
			else
			{
				s.state = eSTATE_FAILED;
				InterlockedIncrement(ctx->pFailed);
			}
			break;
		}
		case en_PACKET_CS_CHAT_RES_MESSAGE:
		{
			s.messagesRecv++;
			InterlockedIncrement(ctx->pTPS_Recv);
			InterlockedIncrement64(ctx->pTotalRecv);
			break;
		}
		case en_PACKET_CS_CHAT_RES_SECTOR_MOVE:
			break;
		default:
			break;
		}

		processed += packetSize;
	}

	return processed;
}

static void HandleRecv(ClientSession& s, WorkerContext* ctx)
{
	int freeSpace = sizeof(s.recvBuf) - s.recvBytes;
	if (freeSpace <= 0)
	{
		// 버퍼 가득 — 비정상
		s.state = eSTATE_DISCONNECTED;
		return;
	}

	int n = recv(s.sock, s.recvBuf + s.recvBytes, freeSpace, 0);
	if (n == 0 || (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK))
	{
		s.state = eSTATE_DISCONNECTED;
		return;
	}
	if (n == SOCKET_ERROR) return;  // WSAEWOULDBLOCK

	s.recvBytes += n;

	int processed = ProcessRecvBuffer(s, ctx);
	if (processed > 0)
	{
		if (processed < s.recvBytes)
			memmove(s.recvBuf, s.recvBuf + processed, s.recvBytes - processed);
		s.recvBytes -= processed;
	}
}

static void HandleSend(ClientSession& s, WorkerContext* ctx)
{
	if (s.sendBytes == 0) return;

	int n = send(s.sock, s.sendBuf, s.sendBytes, 0);
	if (n == SOCKET_ERROR)
	{
		if (WSAGetLastError() == WSAEWOULDBLOCK) return;
		s.state = eSTATE_DISCONNECTED;
		return;
	}

	if (n < s.sendBytes)
		memmove(s.sendBuf, s.sendBuf + n, s.sendBytes - n);
	s.sendBytes -= n;

	InterlockedExchangeAdd(ctx->pTPS_Sent, n);
	InterlockedExchangeAdd64(ctx->pTotalSent, n);
}

unsigned __stdcall WorkerThreadFunc(LPVOID param)
{
	WorkerContext* ctx = (WorkerContext*)param;

	// Phase 1: 모든 세션 connect (blocking, 순차) + non-blocking 전환
	for (int i = 0; i < ctx->sessionCount; i++)
	{
		ClientSession& s = ctx->sessions[i];
		s.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s.sock == INVALID_SOCKET)
		{
			s.state = eSTATE_FAILED;
			InterlockedIncrement(ctx->pFailed);
			continue;
		}

		BOOL nodelay = TRUE;
		setsockopt(s.sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

		if (connect(s.sock, (SOCKADDR*)&ctx->serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
		{
			closesocket(s.sock);
			s.sock = INVALID_SOCKET;
			s.state = eSTATE_FAILED;
			InterlockedIncrement(ctx->pFailed);
			continue;
		}

		u_long mode = 1;
		ioctlsocket(s.sock, FIONBIO, &mode);

		s.state = eSTATE_CONNECTED;
		s.lastHeartbeat = GetTickCount();
		s.lastMessage = GetTickCount();
		InterlockedIncrement(ctx->pConnected);
	}

	// Phase 2: select 루프
	while (WaitForSingleObject(ctx->shutdownEvent, 0) == WAIT_TIMEOUT)
	{
		fd_set readfds, writefds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);

		int activeSocketCnt = 0;
		for (int i = 0; i < ctx->sessionCount; i++)
		{
			ClientSession& s = ctx->sessions[i];
			if (s.sock == INVALID_SOCKET) continue;
			if (s.state >= eSTATE_DISCONNECTED) continue;

			FD_SET(s.sock, &readfds);
			if (s.sendBytes > 0) FD_SET(s.sock, &writefds);
			activeSocketCnt++;
		}

		if (activeSocketCnt == 0)
		{
			Sleep(SELECT_TIMEOUT_MS);
			continue;
		}

		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = SELECT_TIMEOUT_MS * 1000;

		int selectResult = select(0, &readfds, &writefds, NULL, &tv);

		if (selectResult > 0)
		{
			for (int i = 0; i < ctx->sessionCount; i++)
			{
				ClientSession& s = ctx->sessions[i];
				if (s.sock == INVALID_SOCKET) continue;

				if (FD_ISSET(s.sock, &readfds)) HandleRecv(s, ctx);
				if (s.state < eSTATE_DISCONNECTED && FD_ISSET(s.sock, &writefds)) HandleSend(s, ctx);
			}
		}

		// 주기 동작 (로그인/메시지/하트비트)
		DWORD now = GetTickCount();
		for (int i = 0; i < ctx->sessionCount; i++)
		{
			ClientSession& s = ctx->sessions[i];
			if (s.sock == INVALID_SOCKET) continue;

			if (s.state == eSTATE_CONNECTED)
			{
				int globalIdx = ctx->sessionStartIdx + i;
				SendLoginPacket(s, globalIdx);
			}
			else if (s.state == eSTATE_ACTIVE)
			{
				if (now - s.lastMessage >= ctx->messageInterval)
				{
					SendChatPacket(s);
					s.lastMessage = now;
				}
				if (now - s.lastHeartbeat >= HEARTBEAT_INTERVAL_MS)
				{
					SendHeartbeatPacket(s);
					s.lastHeartbeat = now;
				}
			}
			else if (s.state == eSTATE_DISCONNECTED && s.sock != INVALID_SOCKET)
			{
				closesocket(s.sock);
				s.sock = INVALID_SOCKET;
				InterlockedIncrement(ctx->pDisconnected);
			}
		}
	}

	// Cleanup
	for (int i = 0; i < ctx->sessionCount; i++)
	{
		if (ctx->sessions[i].sock != INVALID_SOCKET)
		{
			closesocket(ctx->sessions[i].sock);
			ctx->sessions[i].sock = INVALID_SOCKET;
		}
	}

	return 0;
}
