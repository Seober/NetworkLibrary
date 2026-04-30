#include "PacketCodec.h"

void PacketCodec::SetPacketHeader(CPacket* pPacket)
{
	char* pFront = pPacket->GetReadBufferPtr();
	WORD PacketDataSize = pPacket->GetDataSize();
	BYTE Checksum = 0;
	WORD HeaderSize = sizeof(stHeader_NET);
	pPacket->MoveReadPos(-HeaderSize);

	stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
	pNetHeader->Code = eHEADER_CODE;
	pNetHeader->Len = PacketDataSize;
	pNetHeader->RKey = rand() % 256;

	for (int i = 0; i < PacketDataSize; i++, pFront++) Checksum += *pFront;
	pNetHeader->Checksum = Checksum;
}

void PacketCodec::Encode(CPacket* pPacket)
{
	if (pPacket->CheckFlag_Encode()) return;
	pPacket->LockPacket();
	if (pPacket->CheckFlag_Encode() == false)
	{
		SetPacketHeader(pPacket);
		stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
		BYTE RK = pNetHeader->RKey;
		BYTE P;
		char* pFront = (char*)pNetHeader + sizeof(stHeader_NET) - sizeof(BYTE);

		P = *pFront ^ (RK + 1);
		*pFront = P ^ (eHEADER_KEY + 1);
		pFront++;

		for (int i = 2; i < pNetHeader->Len + 2; i++, pFront++)
		{
			P = *pFront ^ (P + RK + i);
			*pFront = P ^ (*(pFront - 1) + eHEADER_KEY + i);
		}
	}

	pPacket->UnlockPacket();
}

bool PacketCodec::Decode(CPacket* pPacket)
{
	stHeader_NET* pNetHeader = (stHeader_NET*)pPacket->GetReadBufferPtr();
	BYTE RK = pNetHeader->RKey;
	BYTE Checksum = 0;
	unsigned char* pRear = (unsigned char*)pNetHeader + sizeof(stHeader_NET) - sizeof(BYTE) + pNetHeader->Len;
	unsigned char* ptmp = pRear;

	for (int i = pNetHeader->Len + 1; i > 1; i--, ptmp--) *ptmp = *ptmp ^ (*(ptmp - 1) + eHEADER_KEY + i);
	*ptmp = *ptmp ^ (eHEADER_KEY + 1);

	ptmp = pRear;
	for (int i = pNetHeader->Len + 1; i > 1; i--, ptmp--)
	{
		*ptmp = *ptmp ^ (*(ptmp - 1) + RK + i);
		Checksum += *ptmp;
	}
	*ptmp = *ptmp ^ (RK + 1);

	if (pNetHeader->Checksum != Checksum) return false;

	pPacket->MoveReadPos(sizeof(stHeader_NET));
	return true;
}
