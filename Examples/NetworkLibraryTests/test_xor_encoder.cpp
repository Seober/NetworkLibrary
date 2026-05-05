#include "catch_amalgamated.hpp"
#include "XorPacketEncoder.h"
#include "CPacket.h"

#include <vector>
#include <cstring>

// XorPacketEncoder Tier 1 단위 테스트 — 인코딩 알고리즘 회귀 검증의 핵심.
// Encode → Decode 라운드트립 + 검증 함수(VerifyHeaderMagic·PeekPayloadLength) + 실패 경로.

TEST_CASE("XorEncoder: GetHeaderSize returns 5 bytes", "[encoder][peek]") {
    XorPacketEncoder enc;
    REQUIRE(enc.GetHeaderSize() == 5);
}

TEST_CASE("XorEncoder: 1-byte payload roundtrip", "[encoder][roundtrip]") {
    XorPacketEncoder enc(0x77, 0x32);
    CPacket pkt;
    pkt.Clear();
    BYTE original = 0xAB;
    pkt << original;

    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize() + sizeof(BYTE));

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == sizeof(BYTE));

    BYTE recovered = 0;
    pkt >> recovered;
    REQUIRE(recovered == original);
}

TEST_CASE("XorEncoder: empty payload (0 bytes) roundtrip", "[encoder][roundtrip][edge]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    // payload 안 씀

    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize());

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 0);
}

TEST_CASE("XorEncoder: 16-byte payload roundtrip", "[encoder][roundtrip]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    char originalBytes[16];
    for (int i = 0; i < 16; i++)
        originalBytes[i] = (char)(i * 7 + 3);
    pkt.PutData(originalBytes, 16);

    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize() + 16);

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 16);

    char recovered[16];
    pkt.GetData(recovered, 16);
    REQUIRE(memcmp(recovered, originalBytes, 16) == 0);
}

TEST_CASE("XorEncoder: 1000-byte payload roundtrip", "[encoder][roundtrip]") {
    XorPacketEncoder enc;
    CPacket pkt(2048);
    pkt.Clear();
    char originalBytes[1000];
    for (int i = 0; i < 1000; i++)
        originalBytes[i] = (char)((i * 13 + 7) & 0xFF);
    pkt.PutData(originalBytes, 1000);

    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize() + 1000);

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 1000);

    char recovered[1000];
    pkt.GetData(recovered, 1000);
    REQUIRE(memcmp(recovered, originalBytes, 1000) == 0);
}

TEST_CASE("XorEncoder: 4096-byte payload roundtrip (max default)", "[encoder][roundtrip][edge]") {
    XorPacketEncoder enc;
    CPacket pkt(8192);  // header(5) + payload(4096) + reserve(20) = 4121 < 8192
    pkt.Clear();
    char originalBytes[4096];
    for (int i = 0; i < 4096; i++)
        originalBytes[i] = (char)((i * 31 + 11) & 0xFF);
    pkt.PutData(originalBytes, 4096);

    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize() + 4096);

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 4096);

    char recovered[4096];
    pkt.GetData(recovered, 4096);
    REQUIRE(memcmp(recovered, originalBytes, 4096) == 0);
}

TEST_CASE("XorEncoder: all-zero payload roundtrip", "[encoder][roundtrip][pattern]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    char zeros[32] = {0};
    pkt.PutData(zeros, 32);

    enc.Encode(pkt);
    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 32);

    char recovered[32];
    pkt.GetData(recovered, 32);
    REQUIRE(memcmp(recovered, zeros, 32) == 0);
}

TEST_CASE("XorEncoder: all-FF payload roundtrip", "[encoder][roundtrip][pattern]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    char ones[32];
    memset(ones, 0xFF, 32);
    pkt.PutData(ones, 32);

    enc.Encode(pkt);
    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == 32);

    char recovered[32];
    pkt.GetData(recovered, 32);
    REQUIRE(memcmp(recovered, ones, 32) == 0);
}

TEST_CASE("XorEncoder: mixed types roundtrip", "[encoder][roundtrip]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    pkt << (int)42 << (float)3.14f << (__int64)0x123456789ABCDEF0LL << (BYTE)0xFF;

    int expectedSize = sizeof(int) + sizeof(float) + sizeof(__int64) + sizeof(BYTE);
    enc.Encode(pkt);
    REQUIRE(pkt.GetDataSize() == enc.GetHeaderSize() + expectedSize);

    REQUIRE(enc.Decode(pkt));
    REQUIRE(pkt.GetDataSize() == expectedSize);

    int i = 0;
    float f = 0.0f;
    __int64 ll = 0;
    BYTE b = 0;
    pkt >> i >> f >> ll >> b;
    REQUIRE(i == 42);
    REQUIRE(f == 3.14f);
    REQUIRE(ll == 0x123456789ABCDEF0LL);
    REQUIRE(b == 0xFF);
}

TEST_CASE("XorEncoder: Encode idempotency - second call is noop", "[encoder][idempotent]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    pkt << (int)12345;

    enc.Encode(pkt);
    int sizeAfterFirst = pkt.GetDataSize();
    char* p = pkt.GetReadBufferPtr();
    std::vector<char> bytes1(p, p + sizeAfterFirst);

    enc.Encode(pkt);  // 두 번째 호출은 EncodeFlag로 noop
    int sizeAfterSecond = pkt.GetDataSize();
    REQUIRE(sizeAfterSecond == sizeAfterFirst);

    p = pkt.GetReadBufferPtr();
    std::vector<char> bytes2(p, p + sizeAfterSecond);
    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("XorEncoder: Decode rejects corrupted payload byte", "[encoder][decode_fail]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    pkt << (int)42 << (int)100;

    enc.Encode(pkt);

    char* p = pkt.GetReadBufferPtr() + enc.GetHeaderSize();
    *p ^= 0x01;  // 페이로드 1비트 변조

    REQUIRE_FALSE(enc.Decode(pkt));
}

TEST_CASE("XorEncoder: Decode rejects corrupted checksum byte", "[encoder][decode_fail]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    pkt << (int)42;

    enc.Encode(pkt);

    char* p = pkt.GetReadBufferPtr() + 4;  // Checksum byte (offset 4 in header)
    *p ^= 0xFF;

    REQUIRE_FALSE(enc.Decode(pkt));
}

TEST_CASE("XorEncoder: VerifyHeaderMagic accepts correct magic", "[encoder][peek]") {
    XorPacketEncoder enc(0x77, 0x32);
    CPacket pkt;
    pkt.Clear();
    pkt << (int)42;
    enc.Encode(pkt);

    REQUIRE(enc.VerifyHeaderMagic(pkt.GetReadBufferPtr()));
}

TEST_CASE("XorEncoder: VerifyHeaderMagic rejects wrong magic", "[encoder][peek]") {
    XorPacketEncoder enc77(0x77, 0x32);
    XorPacketEncoder enc55(0x55, 0x32);

    CPacket pkt;
    pkt.Clear();
    pkt << (int)42;
    enc55.Encode(pkt);  // 0x55 매직으로 인코딩

    REQUIRE_FALSE(enc77.VerifyHeaderMagic(pkt.GetReadBufferPtr()));
}

TEST_CASE("XorEncoder: PeekPayloadLength returns correct size", "[encoder][peek]") {
    XorPacketEncoder enc;
    CPacket pkt;
    pkt.Clear();
    pkt << (__int64)0x123456789ABCDEF0LL;
    enc.Encode(pkt);

    std::size_t len = 0;
    REQUIRE(enc.PeekPayloadLength(pkt.GetReadBufferPtr(), len));
    REQUIRE(len == sizeof(__int64));
}

TEST_CASE("XorEncoder: PeekPayloadLength rejects oversized payload", "[encoder][peek]") {
    XorPacketEncoder smallMax(0x77, 0x32, 4);  // max 4 byte
    CPacket pkt;
    pkt.Clear();
    pkt << (int)42 << (int)100;  // 8 byte — 한계 초과
    smallMax.Encode(pkt);        // Encode는 max 안 봄, encoded됨

    std::size_t len = 0;
    REQUIRE_FALSE(smallMax.PeekPayloadLength(pkt.GetReadBufferPtr(), len));
}
