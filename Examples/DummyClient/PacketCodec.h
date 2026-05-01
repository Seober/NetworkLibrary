#pragma once
#include <windows.h>
#include "CPacket.h"

class PacketCodec
{
public:
	enum en_NETWORK_PACKET
	{
		eHEADER_CODE = 0x77,
		eHEADER_KEY = 0x32
	};

#pragma pack(push, 1)
	struct stHeader_NET
	{
		BYTE Code;
		WORD Len;
		BYTE RKey;
		BYTE Checksum;
	};
#pragma pack(pop)

	// 헤더 부착 + 페이로드 XOR 암호화 (CNet_Server::SetPacketHeader + EncodePacket 미러)
	static void Encode(CPacket* pPacket);

	// 페이로드 XOR 복호화 + checksum 검증 (CNet_Server::DecodePacket 미러)
	// 성공 시 pPacket의 read pos는 헤더 다음으로 이동됨
	static bool Decode(CPacket* pPacket);

private:
	static void SetPacketHeader(CPacket* pPacket);
};
