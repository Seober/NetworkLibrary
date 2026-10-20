# Phase 4 — Linux 포팅 계획

## 배경

NetworkLibrary는 2023년 Windows IOCP 기반으로 작성됐고, Phase 1~3에서 라이브러리 분리·모던 C++ 변환·벤치마크 하네스 구축을 마쳤다(태그 `phase3-complete`). Phase 4의 목표는 **Windows/Linux 양쪽에서 같은 소스로 빌드되는 라이브러리**다.

최종적으로는 Linux 서버로 운영하는 것이 목표이며, 안정화 전까지는 WSL(Ubuntu 22.04, 커널 6.6)에서 개발·검증한다.

**Phase 3d가 선행 작업을 상당 부분 끝내놨다** — `SRWLOCK`·`CreateThread`·`CreateEvent`·`CriticalSection` 사용처가 0건이고, 이미 `std::shared_mutex`/`std::thread`로 전환돼 있다.

---

## 확정된 설계 결정

### I/O 모델: epoll + proactor 에뮬레이션

IOCP는 **완료 통지**(proactor), epoll은 **준비 통지**(reactor)라 구조가 다르다. epoll 위에 얇은 층을 올려 "완료"처럼 보이게 만든다.

```
[Windows] GetQueuedCompletionStatus  -> (transferred, session, overlapped)
[Linux]   epoll_wait -> EPOLLIN -> recv()로 RecvQ 채움 -> (transferred, session, kRecv)
```

**근거**: 현재 `NetServer::WorkerThread` 약 100줄 중 플랫폼 의존은 3곳뿐이고, 나머지 약 90줄(패킷 파싱·`RecvQ` 관리·`SendBuffer` 해제·세션 ref 카운트)은 그대로 공통이다. reactor로 재작성하면 검증된 동시성 코드를 다시 써야 한다. Boost.Asio가 같은 구조를 쓴다.

**성능 손해는 없다.** reactor로 짜도 `recv()`는 어차피 호출해야 하고, 시스템 콜 횟수가 같다.

### 빌드: CMake 단일 정의

`CMakeLists.txt`가 유일한 빌드 정의. Windows에서는 `cmake -G "Visual Studio 17 2022" -A x64`로 `.sln`을 생성해 기존 작업 방식을 유지한다. 기존 `.sln`/`.vcxproj` 7개는 git에서 제거하고 생성물로 전환한다(`.gitignore` 추가).

**근거**: 빌드 정의가 두 벌이면 한쪽만 고쳐서 다른 쪽이 깨지는 drift가 생긴다. KDevelop·VS Code·CLion·VS 모두 같은 `CMakeLists.txt`를 연다.

### 현 구조를 유지하는 것

| 대상 | 결정 | 근거 |
|---|---|---|
| `AcceptThread` | 전용 스레드 블로킹 `accept` 유지 | Linux도 동일 구조. accept는 초당 수천 건이라 병목 아님 |
| `NetClient::Connect` | 동기(블로킹) 유지 | Windows도 이미 동기. 비동기 connect는 복잡도만 증가 |
| `RecvPost()` 호출 지점 | 그대로 | Windows는 `WSARecv` 재예약, Linux는 `EPOLLONESHOT` 재무장. 호출 위치·시점·횟수 동일 |

---

## 1차 범위 — CMake 전환 + 포팅 가능한 것

**목표**: WSL에서 빌드·테스트가 도는 환경을 먼저 확보한다. 그래야 이후 작업을 Linux에서 직접 반복 검증할 수 있다. epoll 코드는 1차에 한 줄도 쓰지 않는다.

### 1a. 사전 준비 (사용자 실행)

```bash
sudo apt install build-essential cmake gdb
```

### 1b. CMake 도입

- 루트 `CMakeLists.txt` + 타깃별 하위 파일
- 타깃: `BaseLibrary`(static) / `NetworkLibrary`(static) / `NetworkLibraryTests` / `TestServer` / `TestClient` / `ChatServer` / `ChatDummyClient`
- Windows 전용 타깃은 `if(WIN32)`로 감싼다 (1차에선 Examples 전부 Windows 전용)
- `BenchmarkConfig.ini`·`ChatConfig.ini` 복사를 `add_custom_command(POST_BUILD)`로 이전
- `ws2_32`·`winmm`·`DbgHelp` 링크는 `if(WIN32)` 아래

**검증**: Windows에서 생성한 `.sln`으로 **기존과 동일하게 빌드되고 57테스트 통과**. 벤치마크 smoke 1회로 `RecvTPS_p95`가 baseline(106,620) 1% 이내인지 확인.

### 1c. Windows 의존 0인 단위부터 Linux 빌드

| 파일 | Windows 의존 |
|---|---|
| `BaseLibrary/include/Config.h`, `src/Config.cpp` | **0** |
| `BaseLibrary/include/Statistics.h` | **0** |
| `NetworkLibrary/include/IPacketEncoder.h` | **0** |
| `NetworkLibrary/src/XorPacketEncoder.cpp` | **0** |

CMake에 Linux용 테스트 타깃을 만들어 `test_config.cpp`·`test_statistics.cpp`·`test_xor_encoder.cpp`를 돌린다.

**검증**: WSL에서 `cmake && make && ./NetworkLibraryTests` 통과.

### 1d. 타입 추상화

`Packet.h`와 `XorPacketEncoder.h`는 **API 호출 없이 타입만** Windows에 의존한다.

| Windows | 교체 | 비고 |
|---|---|---|
| `BYTE` | `uint8_t` | |
| `WORD` | `uint16_t` | |
| `DWORD` | `uint32_t` | |
| **`WCHAR`** | **`char16_t`** | **아래 주의** |

> **`WCHAR` → `wchar_t`로 바꾸면 안 된다.** Windows `wchar_t`는 16비트(UTF-16), Linux는 32비트(UTF-32)라 **와이어 포맷이 달라진다.** `Packet`은 `operator<<(WCHAR*)`·`GetData(WCHAR*)` 등 문자열 직렬화를 제공하므로, Windows 클라이언트와 Linux 서버가 통신하면 조용히 깨진다. C++11 `char16_t`는 양쪽에서 16비트가 보장되므로 와이어 포맷이 유지된다.
>
> 이 변경은 `Packet`의 공개 인터페이스를 바꾸므로 **ChatServer·ChatDummyClient의 사용처 전수조사**가 필요하다.

### 1e. 동기화 원시형 포팅

| 대상 | 현재 | 교체 | 건수 |
|---|---|---|---|
| `LockFreeQueue`·`LockFreeStack`·`LockFreeMemoryPool` | `InterlockedCompareExchangePointer` 등 | `std::atomic` CAS | 약 45 |
| `TLSMemoryPool` | `TlsAlloc`/`Get`/`Set`/`Free` | `thread_local` | 9 |
| `NetServer`/`NetClient`의 `ReleaseArr` | `InterlockedCompareExchange128` | **2차로 이월** | 2 |

- 16비트 `InterlockedIncrement16`/`Exchange16` → `std::atomic<int16_t>`
- `LockFree*`는 47비트 포인터 패킹에 ABA 태그를 쓴다. x86-64 Linux 유저 주소는 2^47 미만이라 그대로 성립하지만, **ARM64(52비트 VA)에서는 재검토 필요**. 주석에 명시한다.
- `InterlockedCompareExchange128`은 GCC/Clang에서 `__int128` + `-mcx16`로 `cmpxchg16b`를 얻을 수 있다. Phase 3d-2c에서 "Linux 포팅 시 재검토"로 미뤄둔 항목이며, `NetServer` 포팅과 함께 2차에서 다룬다.

**검증**: Linux에서 lock-free 자료구조 테스트 통과 + **TSan/ASan 실행**. MSVC에는 TSan이 없으므로 이 코드들의 데이터 레이스를 처음으로 기계 검증하게 된다.

---

## 2차 이후 (개요)

### 2차 — `NetServer`/`NetClient` epoll 포팅

- `EventLoop` 추상화: `IocpLoop` / `EpollLoop` 두 구현
- `PostQueuedCompletionStatus` → **eventfd**를 epoll에 등록
- `CancelIoEx` → `epoll_ctl(DEL)` + `close()`. Linux가 더 단순함(진행 중 작업 개념이 없음)
- `WSASend`(scatter-gather) → **`sendmsg` + `MSG_NOSIGNAL`**. `writev`는 flags 인자가 없어 `SIGPIPE`를 못 막는다. 시작 시 `signal(SIGPIPE, SIG_IGN)`도 병행
- **부분 전송 상태 기계** — `sendmsg`는 일부만 보낼 수 있다. `Session`에 `SendIndex`/`SendOffset` 추가, `EPOLLOUT`은 부분 전송 시에만 조건부 등록(평소 등록하면 busy loop)
- 멀티스레드 안전: **`EPOLLONESHOT`** — 여러 워커가 같은 fd를 동시 처리하는 것을 막는다. ET로는 해결되지 않는다
- 트리거 모드는 **LT로 시작** (`RecvQ`가 가득 찰 수 있는 구조라 안전)
- 공유 epfd + `maxevents` 중간값으로 시작. 워커 간 부하 불균형이 측정되면 워커별 epfd 재검토

### 3차 — 플랫폼 고유 서브시스템

- `MonitoringTool` (PDH 16건) → `/proc/stat`, `/proc/self/stat`, `/proc/meminfo`
- `CrashDump` (MiniDump 7건) → signal 핸들러 + backtrace. **`Logger`의 멤버 `CrashDump Crasher` 분리도 여기서 함께 정리** (현재 Catch2의 SEH 필터 경고 원인)
- `ChatServer`의 detach된 컨텐츠 스레드 → graceful shutdown
- thread 함수 시그니처 `unsigned WINAPI (LPVOID)` → 플랫폼 중립
- `NetServer::Start`의 `nagle` 인자명 정리 (실제로는 `TCP_NODELAY` 값이라 의미가 반대)

---

## 열린 항목

| 항목 | 결정 시점 |
|---|---|
| CI (GitHub Actions Windows + Linux 러너) | 1차 완료 후. "겸용" 목표의 안전망 |
| Linux에서 Examples를 어떻게 돌릴지 | 2차. `TestServer`/`TestClient`가 epoll 포팅 후 대상 |
| io_uring 백엔드 추가 | 선택. `EventLoop` 추상화가 생기면 같은 인터페이스로 붙일 수 있음 |
| 별도 머신 서버 운영 (데몬화·systemd·로그 로테이션) | 안정화 이후 |

---

## 검증 방법

| 단계 | Windows | Linux |
|---|---|---|
| 1b | `.sln` 빌드 + 57테스트 + smoke 1회 | — |
| 1c | 회귀 없음 확인 | `ctest` 통과 |
| 1d | 57테스트 + ChatServer 기동 | 해당 테스트 통과 |
| 1e | 57테스트 + 벤치마크 30분 | 테스트 + **TSan/ASan** |

**성능 회귀 판정**은 `docs/Benchmark.md` 기준을 따른다 — `RecvTPS_p95`와 `Cpu_p25`를 3회 중앙값으로 비교하며, `Cpu_avg`·`p50`은 쓰지 않는다. baseline은 `RecvTPS_p95` 106,620 / `Cpu_p25` 15.7%.

**Linux에서는 Claude가 직접 빌드·테스트·수정 루프를 돌린다.** Windows 빌드 검증만 사용자가 담당한다.
