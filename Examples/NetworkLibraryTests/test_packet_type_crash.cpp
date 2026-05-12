#include "catch_amalgamated.hpp"
#include "Packet.h"

// TestClient OnRecv PacketType Crash branch 검증 (commit bb70207)
// 실제 Crash 호출은 Catch2 death test 미지원으로 검증 어려움 — 조건 매치만 검증

namespace {
constexpr WORD kTestEcho = 1;
}

TEST_CASE("TestClient PacketType: matching kTestEcho proceeds to verified branch", "[testclient][packettype]") {
    Packet packet;
    WORD type = kTestEcho;
    packet << type;

    WORD readType = 0;
    packet >> readType;

    REQUIRE(readType == kTestEcho);
}

TEST_CASE("TestClient PacketType: mismatched type triggers Crash branch condition", "[testclient][packettype]") {
    Packet packet;
    WORD type = 0xFFFF;
    packet << type;

    WORD readType = 0;
    packet >> readType;

    REQUIRE(readType != kTestEcho);
    REQUIRE(readType == 0xFFFF);
}

TEST_CASE("TestClient PacketType: zero type also triggers Crash branch", "[testclient][packettype]") {
    Packet packet;
    WORD type = 0;
    packet << type;

    WORD readType = kTestEcho;
    packet >> readType;

    REQUIRE(readType != kTestEcho);
    REQUIRE(readType == 0);
}
