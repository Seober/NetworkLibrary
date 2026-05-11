#pragma once
#include <cstddef>

class Packet;

/**
 * @brief 패킷 인코더 인터페이스 — 송신 시 헤더 부착 + 암호화, 수신 시 복호화 + 검증.
 *
 * 라이브러리는 디폴트로 XorPacketEncoder를 사용. 사용자가 IPacketEncoder를 상속한
 * 자체 인코더(AES, ChaCha20 등)를 구현해서 NetServer 생성자에 주입 가능.
 *
 * @note 인코더는 stateless(상수만 보유)일 것을 권장. 다중 스레드가 같은 인스턴스 공유 안전.
 *       상태 보유 시(예: 키 로테이션) 구현체가 자체 동시성 보호 필요.
 */
class IPacketEncoder {
public:
    virtual ~IPacketEncoder() = default;

    /**
     * @brief 패킷에 헤더 부착 + 본문 암호화 (in-place).
     *
     * 사전 조건: packet은 Clear() 호출 후 페이로드만 << 로 채운 상태.
     * 사후 조건: packet.GetReadBufferPtr()부터 packet.GetDataSize() byte가 송신 가능 상태.
     *
     * idempotency: Packet의 EncodeFlag로 보호 — 같은 packet 두 번 호출해도 안전.
     * thread-safety: 같은 packet에 동시 호출 시 SRWLock으로 직렬화. 다른 packet은 병렬 가능.
     */
    virtual void Encode(Packet& packet) = 0;

    /**
     * @brief 본문 복호화 + 인증 (in-place).
     *
     * 사전 조건: packet.GetReadBufferPtr() 위치 = 헤더 시작점 (수신 직후 상태).
     * 성공 시: 페이로드 복원, packet.GetReadBufferPtr() = 페이로드 시작점으로 이동, true 반환.
     * 실패 시: false 반환. packet 데이터 상태 비결정 — 호출자가 폐기해야 함.
     *
     * 호출자 책임: 같은 packet에 두 번 호출하지 말 것 (idempotency 없음).
     */
    virtual bool Decode(Packet& packet) = 0;

    /**
     * @brief 헤더 크기 (raw byte 단위).
     * 수신 측이 메시지 경계 판정 시 사용 — 누적 buffer가 이 크기 이상일 때만 헤더 peek 가능.
     */
    virtual std::size_t GetHeaderSize() const = 0;

    /**
     * @brief raw 헤더 byte로 매직 검증.
     *
     * 잘못된 패킷을 빨리 detect (예: 다른 프로토콜 데이터·corrupted)해서 세션 종료 트리거.
     *
     * @param headerBytes 적어도 GetHeaderSize() byte의 raw 데이터 포인터.
     * @return 매직 byte 일치하면 true. 불일치 시 호출자가 세션 끊는 게 일반.
     */
    virtual bool VerifyHeaderMagic(const void* headerBytes) const = 0;

    /**
     * @brief raw 헤더에서 페이로드 길이 추출.
     *
     * 메시지 경계 판정용 — 누적 buffer에 헤더 + 페이로드 전체가 도착했는지 확인.
     *
     * @param headerBytes 적어도 GetHeaderSize() byte의 raw 데이터 포인터.
     * @param outPayloadLen 성공 시 페이로드 길이 (byte) 채움.
     * @return 정상 길이면 true. 비정상(예: maxPayloadLen 초과)이면 false.
     */
    virtual bool PeekPayloadLength(const void* headerBytes, std::size_t& outPayloadLen) const = 0;
};
