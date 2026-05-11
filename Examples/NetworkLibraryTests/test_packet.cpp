#include "catch_amalgamated.hpp"
#include "Packet.h"

// Packet Tier 1 단위 테스트 — 모더나이제이션 회귀 검증의 90%를 커버하는 핵심 시리얼라이즈 라운드트립.
// Tier 2 (Clear/MovePos/PutData/문자열) 와 Tier 3 (RefCnt) 는 후속 commit에서 추가.

TEST_CASE("Packet: default constructor leaves empty buffer", "[packet][ctor]") {
    Packet p;
    REQUIRE(p.GetBufferSize() == Packet::kBufferDefault);
    REQUIRE(p.GetDataSize() == 0);
    REQUIRE(p.GetFreeSize() == Packet::kBufferDefault);
}

TEST_CASE("Packet: BYTE roundtrip", "[packet][serialize]") {
    Packet p;
    BYTE in = 0xAB;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(BYTE));

    BYTE out = 0;
    p >> out;
    REQUIRE(out == in);
    REQUIRE(p.GetDataSize() == 0);
}

TEST_CASE("Packet: char roundtrip with negative", "[packet][serialize]") {
    Packet p;
    char in = -42;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(char));

    char out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: short roundtrip with negative", "[packet][serialize]") {
    Packet p;
    short in = -12345;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(short));

    short out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: WORD roundtrip with max value", "[packet][serialize]") {
    Packet p;
    WORD in = 0xFFFF;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(WORD));

    WORD out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: int roundtrip with negative", "[packet][serialize]") {
    Packet p;
    int in = -123456789;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(int));

    int out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: DWORD roundtrip with max value", "[packet][serialize]") {
    Packet p;
    DWORD in = 0xFFFFFFFF;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(DWORD));

    DWORD out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: __int64 roundtrip with large value", "[packet][serialize]") {
    Packet p;
    __int64 in = 0x123456789ABCDEF0LL;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(__int64));

    __int64 out = 0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: float roundtrip preserves bit pattern", "[packet][serialize]") {
    Packet p;
    float in = 3.14159f;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(float));

    float out = 0.0f;
    p >> out;
    REQUIRE(out == in);  // memcpy 라운드트립이라 bit-level 일치 기대
}

TEST_CASE("Packet: double roundtrip preserves bit pattern", "[packet][serialize]") {
    Packet p;
    double in = 2.718281828459045;
    p << in;
    REQUIRE(p.GetDataSize() == sizeof(double));

    double out = 0.0;
    p >> out;
    REQUIRE(out == in);
}

TEST_CASE("Packet: mixed-type roundtrip preserves order", "[packet][serialize]") {
    Packet p;
    p << (int)42 << (float)3.14f << (__int64)0x123456789ABCDEF0LL << (BYTE)0xFF;
    REQUIRE(p.GetDataSize() == sizeof(int) + sizeof(float) + sizeof(__int64) + sizeof(BYTE));

    int i = 0;
    float f = 0.0f;
    __int64 ll = 0;
    BYTE b = 0;
    p >> i >> f >> ll >> b;
    REQUIRE(i == 42);
    REQUIRE(f == 3.14f);
    REQUIRE(ll == 0x123456789ABCDEF0LL);
    REQUIRE(b == 0xFF);
    REQUIRE(p.GetDataSize() == 0);
}
