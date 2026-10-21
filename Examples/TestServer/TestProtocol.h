#pragma once
#include <windows.h>

// TestServer/TestClient 공유 프로토콜.
//
// 1차안: PacketType WORD만 + raw byte payload.
// TestServer는 pure echo이므로 PacketType 검증 안 함 (payload 그대로 반환).
// TestClient는 송신 시 payload 첫 WORD에 kTestEcho 넣음 (의도 명시 목적).
//
// 향후 확장 후보:
// - Sequence number (multi-thread echo 시 순서 정합성 검증)
// - Timestamp (round-trip latency 측정)
// - Random payload size (가변 크기 부하 시나리오)
// - Checksum (application layer 정합성 검증)
enum TestPacketType : WORD {
    kTestEcho = 1,
};

// TestServer / TestClient 공유 실행 상한.
// Analysis.Minutes = 0 (무제한) 일 때 두 프로그램이 함께 쓰는 종료 상한이다.
// 샘플을 1초마다 메모리에 모으는 구조라, 상한이 없으면 메모리도 끝없이 늘어난다.
// 이보다 긴 관찰은 벤치 도구가 아니라 별도 수집 도구로 하는 것이 맞다.
const int kUnlimitedRunCapSec = 7 * 24 * 60 * 60;   // 1주 = 604,800초

// 샘플 버퍼 여유분 — 1초 주기 기준 10분치.
// 실행 시간(초) 이 곧 샘플 수 상한이지만, 딱 맞춰 잡으면 그 성질이 종료 조건과
// 샘플 주기에 대한 추론에 의존한다. 나중에 그 둘을 건드리면 측정 끝 무렵에
// 재할당이 한 번 일어나고(가장 큰 복사가 가장 나쁜 시점에), Mem 열의 계단으로
// 나타나 누수와 구분이 안 된다.
// 고정 폭인 이유 — 실제 위험(경계 off-by-a-few, 루프 밖 추가 샘플) 이 실행
// 길이에 비례하지 않는다. 비율로 주면 긴 런에서만 수십 MB 를 헛되이 잡는다.
const int kSampleReserveMargin = 600;
