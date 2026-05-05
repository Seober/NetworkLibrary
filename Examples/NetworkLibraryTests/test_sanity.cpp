#include "catch_amalgamated.hpp"

// Sanity 테스트 — Catch2 link·main 자동 생성 동작 확인
// TEST_CASE 이름은 ASCII 유지 (narrow string literal이 CP949 execution charset에서
// non-ASCII 표현 불가 — C4566. /utf-8 옵션 도입은 Phase 3c·3d에서 일괄 결정)
TEST_CASE("Sanity: Catch2 builds and runs", "[sanity]") {
    REQUIRE(true);
    REQUIRE(1 + 1 == 2);
}
