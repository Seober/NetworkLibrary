#include "catch_amalgamated.hpp"
#include "Statistics.h"

// Statistics 단위 테스트.
// 백분위는 계산이 틀려도 그럴듯한 값이 나와 측정 이력을 조용히 오염시키므로
// nearest-rank 정의(index = ceil(p * N) - 1)를 테스트로 못박는다.

TEST_CASE("Statistics: average", "[statistics]") {
    std::vector<double> values = {10.0, 20.0, 30.0, 40.0};
    REQUIRE(Statistics::Average(values) == Catch::Approx(25.0));
}

TEST_CASE("Statistics: percentile uses nearest-rank", "[statistics]") {
    std::vector<double> values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // N=10 : ceil(p*10) 번째 값 (1-based)
    REQUIRE(Statistics::Percentile(values, 0.50) == Catch::Approx(5.0));    // ceil(5.0)=5
    REQUIRE(Statistics::Percentile(values, 0.90) == Catch::Approx(9.0));    // ceil(9.0)=9
    REQUIRE(Statistics::Percentile(values, 0.95) == Catch::Approx(10.0));   // ceil(9.5)=10
    REQUIRE(Statistics::Percentile(values, 0.99) == Catch::Approx(10.0));   // ceil(9.9)=10
}

TEST_CASE("Statistics: percentile sorts input and keeps caller copy intact", "[statistics]") {
    std::vector<double> values = {50, 10, 40, 20, 30};

    REQUIRE(Statistics::Percentile(values, 0.50) == Catch::Approx(30.0));   // ceil(2.5)=3
    REQUIRE(values[0] == Catch::Approx(50.0));   // 원본은 정렬되지 않아야 함
}

TEST_CASE("Statistics: min and max", "[statistics]") {
    std::vector<double> values = {7.5, -3.0, 12.25, 0.0};
    REQUIRE(Statistics::MinValue(values) == Catch::Approx(-3.0));
    REQUIRE(Statistics::MaxValue(values) == Catch::Approx(12.25));
}

TEST_CASE("Statistics: empty input returns zero", "[statistics]") {
    std::vector<double> empty;
    REQUIRE(Statistics::Average(empty) == Catch::Approx(0.0));
    REQUIRE(Statistics::Percentile(empty, 0.95) == Catch::Approx(0.0));
    REQUIRE(Statistics::MinValue(empty) == Catch::Approx(0.0));
    REQUIRE(Statistics::MaxValue(empty) == Catch::Approx(0.0));
}

TEST_CASE("Statistics: single element", "[statistics]") {
    std::vector<double> single = {42.0};
    REQUIRE(Statistics::Average(single) == Catch::Approx(42.0));
    REQUIRE(Statistics::Percentile(single, 0.50) == Catch::Approx(42.0));
    REQUIRE(Statistics::Percentile(single, 0.99) == Catch::Approx(42.0));
    REQUIRE(Statistics::MinValue(single) == Catch::Approx(42.0));
    REQUIRE(Statistics::MaxValue(single) == Catch::Approx(42.0));
}
