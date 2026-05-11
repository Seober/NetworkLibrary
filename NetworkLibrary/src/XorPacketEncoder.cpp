#include "XorPacketEncoder.h"
#include <cstdlib>  // rand

XorPacketEncoder::XorPacketEncoder(BYTE headerCode, BYTE encryptKey, WORD maxPayloadLen)
    : HeaderCode(headerCode), EncryptKey(encryptKey), MaxPayloadLen(maxPayloadLen) {}

void XorPacketEncoder::Encode(Packet& packet) {
    // idempotency: 이미 Encode된 packet은 다시 처리 안 함 (재전송 시나리오 보호)
    if (packet.CheckFlag_Encode())
        return;

    packet.LockPacket();
    if (packet.CheckFlag_Encode() == false) {
        // 헤더 작성 — 페이로드 위치 미리 잡고, Front를 헤더 공간으로 후진
        char* payload = packet.GetReadBufferPtr();
        WORD payloadSize = (WORD)packet.GetDataSize();
        BYTE checksum = 0;

        packet.MoveReadPos(-(int)sizeof(NetHeader));
        NetHeader* header = (NetHeader*)packet.GetReadBufferPtr();
        header->Code = HeaderCode;
        header->Len = payloadSize;
        header->RKey = (BYTE)(rand() % 256);

        for (WORD i = 0; i < payloadSize; i++)
            checksum += payload[i];
        header->Checksum = checksum;

        // XOR 암호화 — Checksum byte부터 페이로드 끝까지
        BYTE RK = header->RKey;
        BYTE P;
        char* p = (char*)header + sizeof(NetHeader) - sizeof(BYTE);  // Checksum 위치

        // 첫 byte (Checksum)
        P = *p ^ (RK + 1);
        *p = P ^ (EncryptKey + 1);
        p++;

        // 페이로드 byte들
        for (int i = 2; i < payloadSize + 2; i++, p++) {
            P = *p ^ (P + RK + i);
            *p = P ^ (*(p - 1) + EncryptKey + i);
        }
    }
    packet.UnlockPacket();
}

bool XorPacketEncoder::Decode(Packet& packet) {
    NetHeader* header = (NetHeader*)packet.GetReadBufferPtr();
    BYTE RK = header->RKey;
    BYTE checksum = 0;

    unsigned char* rear =
        (unsigned char*)header + sizeof(NetHeader) - sizeof(BYTE) + header->Len;
    unsigned char* tmp = rear;

    // 첫 루프 — encryptKey XOR 풀기 (역순)
    for (int i = header->Len + 1; i > 1; i--, tmp--)
        *tmp = *tmp ^ (*(tmp - 1) + EncryptKey + i);
    *tmp = *tmp ^ (EncryptKey + 1);

    // 둘째 루프 — RKey XOR 풀기 + checksum 계산 (역순)
    tmp = rear;
    for (int i = header->Len + 1; i > 1; i--, tmp--) {
        *tmp = *tmp ^ (*(tmp - 1) + RK + i);
        checksum += *tmp;
    }
    *tmp = *tmp ^ (RK + 1);

    if (header->Checksum != checksum)
        return false;

    // 성공 — Front를 페이로드 시작 위치로 이동 (호출자가 페이로드만 직접 read 가능)
    packet.MoveReadPos((int)sizeof(NetHeader));
    return true;
}

std::size_t XorPacketEncoder::GetHeaderSize() const {
    return sizeof(NetHeader);
}

bool XorPacketEncoder::VerifyHeaderMagic(const void* headerBytes) const {
    const NetHeader* header = (const NetHeader*)headerBytes;
    return header->Code == HeaderCode;
}

bool XorPacketEncoder::PeekPayloadLength(const void* headerBytes,
                                         std::size_t& outPayloadLen) const {
    const NetHeader* header = (const NetHeader*)headerBytes;
    if (MaxPayloadLen != 0 && header->Len > MaxPayloadLen)
        return false;
    outPayloadLen = header->Len;
    return true;
}
