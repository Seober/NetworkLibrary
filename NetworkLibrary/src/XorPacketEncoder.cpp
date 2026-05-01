#include "XorPacketEncoder.h"
#include <cstdlib>  // rand

XorPacketEncoder::XorPacketEncoder(BYTE headerCode, BYTE encryptKey, WORD maxPayloadLen)
    : m_headerCode(headerCode), m_encryptKey(encryptKey), m_maxPayloadLen(maxPayloadLen)
{
}

void XorPacketEncoder::Encode(CPacket& packet)
{
    // idempotency: 이미 Encode된 packet은 다시 처리 안 함 (재전송 시나리오 보호)
    if (packet.CheckFlag_Encode()) return;

    packet.LockPacket();
    if (packet.CheckFlag_Encode() == false)
    {
        // 헤더 작성 — 페이로드 위치 미리 잡고, Front를 헤더 공간으로 후진
        char* pPayload = packet.GetReadBufferPtr();
        WORD payloadSize = (WORD)packet.GetDataSize();
        BYTE checksum = 0;

        packet.MoveReadPos(-(int)sizeof(NetHeader));
        NetHeader* pHeader = (NetHeader*)packet.GetReadBufferPtr();
        pHeader->Code = m_headerCode;
        pHeader->Len = payloadSize;
        pHeader->RKey = (BYTE)(rand() % 256);

        for (WORD i = 0; i < payloadSize; i++)
            checksum += pPayload[i];
        pHeader->Checksum = checksum;

        // XOR 암호화 — Checksum byte부터 페이로드 끝까지
        BYTE RK = pHeader->RKey;
        BYTE P;
        char* p = (char*)pHeader + sizeof(NetHeader) - sizeof(BYTE);  // Checksum 위치

        // 첫 byte (Checksum)
        P = *p ^ (RK + 1);
        *p = P ^ (m_encryptKey + 1);
        p++;

        // 페이로드 byte들
        for (int i = 2; i < payloadSize + 2; i++, p++)
        {
            P = *p ^ (P + RK + i);
            *p = P ^ (*(p - 1) + m_encryptKey + i);
        }
    }
    packet.UnlockPacket();
}

bool XorPacketEncoder::Decode(CPacket& packet)
{
    NetHeader* pHeader = (NetHeader*)packet.GetReadBufferPtr();
    BYTE RK = pHeader->RKey;
    BYTE checksum = 0;

    unsigned char* pRear = (unsigned char*)pHeader + sizeof(NetHeader) - sizeof(BYTE) + pHeader->Len;
    unsigned char* pTmp = pRear;

    // 첫 루프 — encryptKey XOR 풀기 (역순)
    for (int i = pHeader->Len + 1; i > 1; i--, pTmp--)
        *pTmp = *pTmp ^ (*(pTmp - 1) + m_encryptKey + i);
    *pTmp = *pTmp ^ (m_encryptKey + 1);

    // 둘째 루프 — RKey XOR 풀기 + checksum 계산 (역순)
    pTmp = pRear;
    for (int i = pHeader->Len + 1; i > 1; i--, pTmp--)
    {
        *pTmp = *pTmp ^ (*(pTmp - 1) + RK + i);
        checksum += *pTmp;
    }
    *pTmp = *pTmp ^ (RK + 1);

    if (pHeader->Checksum != checksum)
        return false;

    // 성공 — Front를 페이로드 시작 위치로 이동 (PacketCodec 동작과 통일)
    packet.MoveReadPos((int)sizeof(NetHeader));
    return true;
}

std::size_t XorPacketEncoder::GetHeaderSize() const
{
    return sizeof(NetHeader);
}

bool XorPacketEncoder::VerifyHeaderMagic(const void* headerBytes) const
{
    const NetHeader* pHeader = (const NetHeader*)headerBytes;
    return pHeader->Code == m_headerCode;
}

bool XorPacketEncoder::PeekPayloadLength(const void* headerBytes, std::size_t& outPayloadLen) const
{
    const NetHeader* pHeader = (const NetHeader*)headerBytes;
    if (m_maxPayloadLen != 0 && pHeader->Len > m_maxPayloadLen)
        return false;
    outPayloadLen = pHeader->Len;
    return true;
}
