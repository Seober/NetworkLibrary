#pragma once
#include "CLan_Client.h"
#include "Protocol.h"

class MonitorClient : public CLan_Client
{
public:
	MonitorClient(BYTE _ServerNo) : ConnectionState(false), ServerNo(_ServerNo) {};


	virtual void OnEnterJoinServer(void) { ConnectionState = true; }
	virtual void OnLeaveServer(void) { ConnectionState = false; }

	virtual void OnRecv(CPacket* pPacket) {}
	virtual void OnSend(int iSendSize) {}

	bool GetConnectionState(void) { return ConnectionState; }

	void Login(void)
	{
		WORD MessageType = en_PACKET_SS_MONITOR_LOGIN;
		CPacket* pPacket = AllocPacket();
		*pPacket << MessageType << ServerNo;
		SendPacket(pPacket);
		FreePacket(pPacket);
	}

	void SendLogMessage(BYTE DataType, int DataValue, int TimeStamp)
	{
		if (ConnectionState == false) return;

		CPacket* pPacket = AllocPacket();
		WORD MessageType = en_PACKET_SS_MONITOR_DATA_UPDATE;
		*pPacket << MessageType << DataType << DataValue << TimeStamp;
		SendPacket(pPacket);
		FreePacket(pPacket);
	}

private:
	bool ConnectionState;
	BYTE ServerNo;
};