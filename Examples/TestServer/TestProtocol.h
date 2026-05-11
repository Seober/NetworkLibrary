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
