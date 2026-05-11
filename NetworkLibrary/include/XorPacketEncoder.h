#pragma once
#include "IPacketEncoder.h"
#include "Packet.h"
#include <windows.h>

/**
 * @brief XOR + checksum 기반 패킷 인코더 — 라이브러리 디폴트 인코더.
 *
 * 5 byte 헤더 (Code/Len/RKey/Checksum) + per-packet random key XOR 암호화.
 * 약식 obfuscation 수준 — 정규 암호화 필요 시 IPacketEncoder 상속해서 자체 구현 권장.
 *
 * @note thread-safe: 인스턴스가 stateless (상수만 보유)이라 다중 스레드 공유 안전.
 */
class XorPacketEncoder : public IPacketEncoder {
public:
    /**
     * @brief 생성자.
     * @param headerCode 매직 byte (수신 측 검증용). 일반적으로 0x77.
     * @param encryptKey XOR 암호화 키. 일반적으로 0x32.
     * @param maxPayloadLen PeekPayloadLength sanity check 상한 (byte). 0이면 무제한.
     */
    XorPacketEncoder(BYTE headerCode = 0x77, BYTE encryptKey = 0x32, WORD maxPayloadLen = 4096);

    void Encode(Packet& packet) override;
    bool Decode(Packet& packet) override;

    std::size_t GetHeaderSize() const override;
    bool VerifyHeaderMagic(const void* headerBytes) const override;
    bool PeekPayloadLength(const void* headerBytes, std::size_t& outPayloadLen) const override;

private:
#pragma pack(push, 1)
    struct NetHeader {
        BYTE Code;
        WORD Len;
        BYTE RKey;
        BYTE Checksum;
    };
#pragma pack(pop)

    // 헤더가 Packet의 reserve 공간(kHeaderDefault)에 들어가야 함.
    // 더 큰 헤더 인코더 도입 시 Packet API 확장 필요 (Phase 3b/3c).
    static_assert(sizeof(NetHeader) <= Packet::kHeaderDefault,
                  "XorPacketEncoder NetHeader exceeds Packet reserve space (kHeaderDefault)");

    BYTE HeaderCode;
    BYTE EncryptKey;
    WORD MaxPayloadLen;
};
