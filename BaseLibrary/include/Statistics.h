#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// 측정 샘플 통계 — 순수 함수 모음.
//
// 백분위는 nearest-rank 방식: 정렬 후 index = ceil(p * N) - 1.
// 계산이 틀려도 "그럴듯한 숫자"가 나와 조용히 측정 이력을 오염시키므로
// 별도 파일로 분리해 단위 테스트로 고정한다.
// TestServer·TestClient가 같은 구현을 공유해 두 결과가 어긋나지 않게 한다.
namespace Statistics {

inline double Average(const std::vector<double>& values) {
    if (values.empty())
        return 0.0;

    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / (double)values.size();
}

// p는 0.0 ~ 1.0 (예: 0.95 = 95 백분위). 내부 정렬이 필요해 값으로 받는다.
inline double Percentile(std::vector<double> values, double p) {
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());

    if (p <= 0.0)
        return values.front();
    if (p >= 1.0)
        return values.back();

    std::size_t rank = (std::size_t)std::ceil(p * (double)values.size());
    if (rank < 1)
        rank = 1;
    if (rank > values.size())
        rank = values.size();
    return values[rank - 1];
}

inline double MinValue(const std::vector<double>& values) {
    if (values.empty())
        return 0.0;
    return *std::min_element(values.begin(), values.end());
}

inline double MaxValue(const std::vector<double>& values) {
    if (values.empty())
        return 0.0;
    return *std::max_element(values.begin(), values.end());
}

}   // namespace Statistics
