# NetworkLibrary

IOCP 기반 C++ 네트워크 라이브러리. 2023년에 채팅서버와 함께 작성된 코드에서 라이브러리 부분을 분리하여 재사용 가능한 형태로 정리하는 중.

최종 목표: Windows/Linux(epoll) 크로스플랫폼 + 모던 C++(11/14/17) 컨버팅.

## 현재 상태

| 단계 | 내용 | 상태 |
|---|---|---|
| Phase 0 | 워크스페이스 준비 (브랜치, CLAUDE.md) | 진행 중 |
| Phase 1+2 | 라이브러리 분리 + 결함 검토 병행 | 대기 |
| Phase 3 | 리팩토링 + BaseLibrary 분리 + 모던 C++ 컨버팅 | 예정 |
| Phase 4 | Linux 포팅 (CMake, epoll, 플랫폼 추상화) | 예정 |

상세 실행 계획: `~/.claude/plans/23-iocp-piped-honey.md`

## 디렉토리 구조

**현재** (Phase 0):
```
NetworkLibrary/
├─ Chat_Server.sln
└─ Chat_Server/         ← 라이브러리 + 채팅서버 코드 혼재
```

**Phase 1+2 종료 후 목표**:
```
NetworkLibrary/
├─ NetworkLibrary.sln
├─ NetworkLibrary/      ← Static Library
│  ├─ NetworkLibrary.vcxproj
│  ├─ include/
│  └─ src/
└─ Examples/
   └─ ChatServer/       ← 라이브러리 사용 예제 겸 회귀 테스트
      └─ ChatServer.vcxproj
```

(Phase 3에서 `LockFree_*`, `MemoryPool_*`, `Logger`, `CrashDump`는 별도 BaseLibrary로 재분할 예정)

## 빌드

**Windows (현재)**: Visual Studio 2022 (v143 toolset). `Chat_Server.sln` → `Debug|x64` 또는 `Release|x64`. Phase 1+2 종료 후 `NetworkLibrary.sln` 으로 이름 변경.

**Linux (Phase 4)**: CMake 도입 예정.

### 외부 의존성 (Windows)
- `ws2_32.lib` — Winsock2 (IOCP, WSARecv/WSASend)
- `winmm.lib` — 멀티미디어 타이머
- `DbgHelp.lib` — MiniDump

## 코드 스타일

**현재** (Phase 1+2 동안 유지):
- C++14 추정 (vcxproj에 `/std:c++` 명시 없음, v143 디폴트)
- 헝가리안 표기법 (`m_iBufferSize`, `dwSendFlag`, `pSession`)
- `C` 접두사 클래스명 (`CNet_Server`, `CPacket`)
- raw pointer + 수동 `new/delete`
- Windows 동기화 원시형 (`SRWLock`, `Interlocked*`, `TlsAlloc`)

**목표** (Phase 3 일괄 정리):
- `nullptr`, `enum class`, `constexpr`, `auto`, range-for, lambda
- `std::atomic`, `std::shared_mutex`, `std::thread`, `thread_local`
- 스마트 포인터 (`unique_ptr`, `shared_ptr`)
- 모던 명명 컨벤션

## Claude와 함께 작업할 때 (협업 규칙)

다음 규칙은 사용자가 명시적으로 정의한 것이며, 모든 작업에 우선합니다:

1. **코드 변경 전 항상 사전 승인** — 한 줄 변경이라도 동일
2. **git commit/push 전 사용자 확인** — 커밋 메시지는 **한글로 제안** 후 승인 (아래 "커밋 메시지 컨벤션" 참고)
3. **CLAUDE.md 변경도 사전 확인**
4. **결함 단정 금지** — "의심 → 정독 → 의도 확인 → 합의" 절차 준수. 23년 코드의 의심 패턴 다수가 사용자 의도일 수 있음
5. **100% 확신 없으면 짐작·추론 금지** — 사용자에게 묻거나 코드·공식 문서·표준 명세로 확정 후 진행

### 커밋 메시지 컨벤션

Conventional Commits 형식 (영어 타입 + 한글 description):

```
<타입>: <한글 description>

(본문 — 필요 시)
```

사용 타입 (Conventional Commits 표준):
- `feat` — 새 기능
- `fix` — 버그·오타·결함 수정
- `docs` — 문서 변경
- `style` — 코드 스타일 (포맷팅, 공백)
- `refactor` — 동작 변화 없는 구조 개선·코드 정리
- `perf` — 성능 개선
- `test` — 테스트 추가·수정
- `build` — 빌드 시스템·외부 의존성 (vcxproj, sln, CMake)
- `ci` — CI 설정
- `chore` — 그 외 잡일

규칙:
- description은 한글, **마침표 없음**, 명사형 종결 (`추가`, `제거`, `재구성`)
- en-dash(`—`)로 부가 설명 구분
- 가운뎃점(`·`)으로 항목 나열
- 영어 식별자(파일명·클래스명·vcxproj 등)는 그대로 표기
- 본문이 필요할 때만 추가 (단순 cleanup은 제목만)
- BREAKING CHANGE 표기는 `feat!:` 또는 footer `BREAKING CHANGE:`

도구 호환:
- commitlint, semantic-release, conventional-changelog 등 표준 도구와 호환
- commitlint의 한글-비호환 규칙(`subject-case`, `subject-full-stop`)은 설정으로 disable

라벨이 모호하거나 빠진 경우는 그때 보완 → Phase 1+2 종료 시 정리해서 user-level로 승격.

### 환경 한계 (작업 분담)

| WSL 측 (Claude) | Windows 측 (사용자) |
|---|---|
| 코드 정독, grep, 정적 분석 | MSVC 빌드 |
| 호출 그래프·보호 구조 정리 | IOCP·Winsock·MiniDump 실행 |
| vcxproj/sln 텍스트 작성·수정 | 동시성 레이스 런타임 재현 |
| 사용자가 보낸 빌드 로그·덤프 분석 | 채팅서버 장기 안정성 테스트 |

→ Phase 1+2 동안 빌드·실행 검증은 사용자 책임. 자동 빌드 파이프라인(GitHub Actions Windows runner) 도입은 Phase 3 진입 시 재검토.

## 참고

- GitHub: https://github.com/Seober/NetworkLibrary.git
- Plan 파일: `~/.claude/plans/23-iocp-piped-honey.md`
- 작업 브랜치: `library-only`
