# NetworkLibrary

IOCP 기반 C++ 네트워크 라이브러리. 2023년에 채팅서버와 함께 작성된 코드에서 라이브러리 부분을 분리하여 재사용 가능한 형태로 정리하는 중.

최종 목표: Windows/Linux(epoll) 크로스플랫폼 + 모던 C++(11/14/17) 컨버팅.

> **공통 협업·커밋·코드 스타일 규칙은 상위 `../CLAUDE.md` 참고.** 본 파일은 NetworkLibrary 한정 컨텍스트만 정리.

## 현재 상태

| 단계 | 내용 | 상태 |
|---|---|---|
| Phase 0 | 워크스페이스 준비 (브랜치, CLAUDE.md) | 완료 |
| Phase 1+2 | 라이브러리 분리 + 결함 검토 병행 | 완료 (2026-05-01) |
| Phase 3 | BaseLibrary 분리 + 모던 C++ 컨버팅 | 진행 중 (3a Catch2 도입) |
| Phase 4 | Linux 포팅 (CMake, epoll, 플랫폼 추상화) | 예정 |

상세 실행 계획:
- Phase 1+2: `~/.claude/plans/23-iocp-piped-honey.md`
- Phase 3: `~/.claude/plans/24-phase3-modernization.md`

## 디렉토리 구조

```
NetworkLibrary/
├─ NetworkLibrary.sln
├─ NetworkLibrary/      ← Static Library
│  ├─ NetworkLibrary.vcxproj
│  ├─ include/
│  └─ src/
├─ External/            ← 3rd-party (Phase 3a부터)
│  └─ Catch2/           ← 단위 테스트 프레임워크 amalgamated
└─ Examples/
   ├─ ChatServer/       ← 라이브러리 사용 예제 겸 회귀 테스트
   │  └─ ChatServer.vcxproj
   ├─ DummyClient/      ← 부하·안정성 테스트용 (select 모델, N개 동시접속)
   │  └─ DummyClient.vcxproj
   └─ NetworkLibraryTests/  ← 단위 테스트 (Phase 3a부터)
      └─ NetworkLibraryTests.vcxproj
```

(Phase 3b에서 `LockFree_*`, `MemoryPool_*`, `Logger`, `CrashDump`는 별도 BaseLibrary로 재분할 예정)

## 빌드

**Windows (현재)**: Visual Studio 2026 (v145 toolset). `NetworkLibrary.sln` → `Debug|x64` 또는 `Release|x64`. 솔루션은 `NetworkLibrary`(.lib), `ChatServer`(.exe), `DummyClient`(.exe) 3개 프로젝트 포함.

Phase 3a부터 `NetworkLibraryTests`(.exe) 추가, Phase 3b부터 `BaseLibrary`(.lib) 분리.

**Linux (Phase 4)**: CMake 도입 예정.

### 외부 의존성 (Windows)
- `ws2_32.lib` — Winsock2 (IOCP, WSARecv/WSASend)
- `winmm.lib` — 멀티미디어 타이머
- `DbgHelp.lib` — MiniDump

## 코드 스타일

**현재** (Phase 3c 적용 전):
- C++14 추정 (vcxproj에 `/std:c++` 명시 없음, v145 디폴트)
- 헝가리안 표기법 (`m_iBufferSize`, `dwSendFlag`, `pSession`)
- `C` 접두사 클래스명 (`CNet_Server`, `CPacket`)
- raw pointer + 수동 `new/delete`
- Windows 동기화 원시형 (`SRWLock`, `Interlocked*`, `TlsAlloc`)

**Phase 3c부터 적용**: Google C++ Style (상세 규칙은 상위 `../CLAUDE.md` 참고). `.clang-format` 자동화.

**Phase 3d부터 적용**: 모던 C++ 변환 (`nullptr`, `enum class`, `std::atomic`, `std::shared_mutex`, `std::thread`, `unique_ptr` — `shared_ptr` 핫패스 금지). 상세는 Phase 3 plan 참고.

## 참고

- GitHub: https://github.com/Seober/NetworkLibrary.git
- Plan 파일: `~/.claude/plans/23-iocp-piped-honey.md` (Phase 1+2), `~/.claude/plans/24-phase3-modernization.md` (Phase 3)
- 작업 브랜치: master (Phase 1+2 종료 후), Phase 3 sub-phase 시작 시 브랜치 전략 재결정
- 공통 규칙: 상위 `../CLAUDE.md`
