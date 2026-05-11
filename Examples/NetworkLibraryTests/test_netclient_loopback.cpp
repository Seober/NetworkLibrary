#include "catch_amalgamated.hpp"
#include "NetServer.h"
#include "NetClient.h"
#include "Packet.h"

#include <atomic>
#include <chrono>
#include <thread>

// NetClient 단위 테스트 — NetServer + NetClient 동일 프로세스 loopback 시나리오.
// Connect → SendPacket → Server echo → Client OnRecv 검증 → Disconnect → OnDisconnect 검증.
//
// 주의: 본 테스트는 process exit 시 OS가 worker thread 자원 정리에 의존.
// 정상 종료 절차 (PostQueuedCompletionStatus로 worker break)는 별도 phase에서 처리 예정.

namespace {

constexpr u_short kTestPort = 12998;
constexpr WCHAR kTestIP[] = L"127.0.0.1";

class EchoServer : public NetServer {
public:
    bool OnConnectionRequest() override { return true; }

    void OnClientJoin(unsigned __int64 sessionID) override {
        JoinCnt++;
    }

    void OnClientLeave(unsigned __int64 sessionID) override {
        LeaveCnt++;
    }

    void OnRecv(unsigned __int64 sessionID, Packet* packet) override {
        // echo — 받은 페이로드를 새 packet에 담아 송신
        Packet* echo = AllocPacket();
        echo->PutData(packet->GetReadBufferPtr(), packet->GetDataSize());
        SendPacket(sessionID, echo);
        FreePacket(echo);
        RecvCnt++;
    }

    std::atomic<int> JoinCnt{0};
    std::atomic<int> LeaveCnt{0};
    std::atomic<int> RecvCnt{0};
};

class LoopbackClient : public NetClient {
public:
    static constexpr int kTestValue = 0x12345678;

    void OnConnect(unsigned __int64 sessionID) override {
        ConnectCnt++;
    }

    void OnDisconnect(unsigned __int64 sessionID) override {
        DisconnectCnt++;
    }

    void OnRecv(unsigned __int64 sessionID, Packet* packet) override {
        RecvCnt++;
        int v;
        *packet >> v;
        if (v == kTestValue)
            VerifiedCnt++;
    }

    std::atomic<int> ConnectCnt{0};
    std::atomic<int> DisconnectCnt{0};
    std::atomic<int> RecvCnt{0};
    std::atomic<int> VerifiedCnt{0};
};

}  // namespace

TEST_CASE("NetClient: loopback echo + disconnect", "[netclient][loopback]") {
    // 단위 테스트 환경 우회 — EchoServer/LoopbackClient를 heap에 할당 + 의도된 leak.
    // stack alloc 시 ~NetServer 본체 실행 중 vtable이 NetServer로 reset된 상태에서
    // AcceptThread가 virtual OnConnectionRequest 호출하면 pure virtual call이 발생
    // (myPurecallHandler → Crash). process exit 시 OS가 자원 정리.
    // 본격 해결 (Stop 메서드 + worker/accept thread 정상 종료)은 Phase 3c-8b.
    auto* server = new EchoServer;
    REQUIRE(server->Start(kTestIP, kTestPort, 2, 0, FALSE, 4));

    // Server listen 안정화 대기
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto* client = new LoopbackClient;
    REQUIRE(client->Start(2, 0, 4));

    // 동기 Connect — 성공 시 caller thread에서 OnConnect 콜백 호출됨
    unsigned __int64 sid = client->Connect(kTestIP, kTestPort);
    REQUIRE(sid != 0);
    REQUIRE(client->ConnectCnt == 1);

    // Server 측 OnClientJoin 처리 대기 (worker thread 비동기)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(server->JoinCnt == 1);

    // Echo round-trip: client → server (echo) → client
    Packet* p = client->AllocPacket();
    int v = LoopbackClient::kTestValue;
    *p << v;
    client->SendPacket(sid, p);
    client->FreePacket(p);

    // Send → server recv → echo → client recv 라운드트립 대기
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    REQUIRE(server->RecvCnt == 1);
    REQUIRE(client->RecvCnt == 1);
    REQUIRE(client->VerifiedCnt == 1);

    // Disconnect 검증
    client->Disconnect(sid);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(client->DisconnectCnt == 1);
    REQUIRE(server->LeaveCnt == 1);

    // server/client delete 안 함 — process exit cleanup 의존 (Phase 3c-8b에서 정식 변경)
}
