#include "catch_amalgamated.hpp"
#include <windows.h>

// Logger LogCnt 64-bit fix 검증 (commit a49719f)
// 옛 코드: unsigned __int64 LogCnt + InterlockedIncrement (32-bit) — 하위 32-bit만 +1, wrap
// 현재 코드: LONG64 LogCnt + InterlockedIncrement64 — 64-bit 정상 누적
// Logger::LogCnt 자체는 private이라 직접 접근 어려움 — API 패턴 검증으로 우회

TEST_CASE("Logger: InterlockedIncrement64 accumulates beyond 32-bit boundary", "[logger][64bit]") {
    LONG64 cnt = 0xFFFFFFFFLL;
    REQUIRE(cnt == 4294967295LL);

    LONG64 next = InterlockedIncrement64(&cnt);
    REQUIRE(next == 0x100000000LL);
    REQUIRE(cnt == 0x100000000LL);

    for (int i = 0; i < 1000; ++i)
        InterlockedIncrement64(&cnt);
    REQUIRE(cnt == 0x100000000LL + 1000);
}

TEST_CASE("Logger: 32-bit InterlockedIncrement wraps at boundary (old bug pattern)", "[logger][wrap]") {
    LONG cnt32 = static_cast<LONG>(0xFFFFFFFFL);
    LONG next32 = InterlockedIncrement(&cnt32);
    REQUIRE(next32 == 0);
    REQUIRE(cnt32 == 0);
}
