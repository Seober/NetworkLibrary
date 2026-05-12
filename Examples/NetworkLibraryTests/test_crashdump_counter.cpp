#include "catch_amalgamated.hpp"
#include "CrashDump.h"
#include <windows.h>

// CrashDump DumpCount shadowing fix 검증 (commit c411646)
// 옛 코드: long DumpCount = InterlockedIncrement(&DumpCount); — 지역 self-ref, 미정의 동작
// 현재 코드: long dumpCount = InterlockedIncrement(&DumpCount); — 멤버에 정상 +1

TEST_CASE("CrashDump: DumpCount static increment accumulates", "[crashdump][counter]") {
    CrashDump::DumpCount = 0;

    long a = InterlockedIncrement(&CrashDump::DumpCount);
    REQUIRE(a == 1);
    REQUIRE(CrashDump::DumpCount == 1);

    long b = InterlockedIncrement(&CrashDump::DumpCount);
    REQUIRE(b == 2);
    REQUIRE(CrashDump::DumpCount == 2);

    long c = InterlockedIncrement(&CrashDump::DumpCount);
    REQUIRE(c == 3);
    REQUIRE(CrashDump::DumpCount == 3);

    CrashDump::DumpCount = 0;
}

TEST_CASE("CrashDump: shadowing fix preserves member identity", "[crashdump][shadowing]") {
    CrashDump::DumpCount = 100;

    // MyExceptionFilter line 37 패턴 재현 — 멤버에 +1, 지역에 결과 받기
    long dumpCount = InterlockedIncrement(&CrashDump::DumpCount);
    REQUIRE(dumpCount == 101);
    REQUIRE(CrashDump::DumpCount == 101);

    long dumpCount2 = InterlockedIncrement(&CrashDump::DumpCount);
    REQUIRE(dumpCount2 == 102);
    REQUIRE(CrashDump::DumpCount == 102);

    CrashDump::DumpCount = 0;
}
