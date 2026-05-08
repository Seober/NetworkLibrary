#pragma once
#include <windows.h>

// Chat 패킷 ID (Client ↔ Server)
// wire format은 WORD (2 byte unsigned)
enum PacketType : WORD {
    kCsChatServer = 0,

    //------------------------------------------------------------
    // 채팅서버 로그인 요청
    //  {
    //      WORD    Type
    //      INT64   AccountNo
    //      WCHAR   ID[20]              // null 포함
    //      WCHAR   Nickname[20]        // null 포함
    //      char    SessionKey[64];     // 인증토큰
    //  }
    //------------------------------------------------------------
    kCsChatReqLogin,

    //------------------------------------------------------------
    // 채팅서버 로그인 응답
    //  {
    //      WORD    Type
    //      BYTE    Status              // 0:실패  1:성공
    //      INT64   AccountNo
    //  }
    //------------------------------------------------------------
    kCsChatResLogin,

    //------------------------------------------------------------
    // 채팅서버 섹터 이동 요청
    //  {
    //      WORD    Type
    //      INT64   AccountNo
    //      WORD    SectorX
    //      WORD    SectorY
    //  }
    //------------------------------------------------------------
    kCsChatReqSectorMove,

    //------------------------------------------------------------
    // 채팅서버 섹터 이동 결과
    //  {
    //      WORD    Type
    //      INT64   AccountNo
    //      WORD    SectorX
    //      WORD    SectorY
    //  }
    //------------------------------------------------------------
    kCsChatResSectorMove,

    //------------------------------------------------------------
    // 채팅서버 채팅보내기 요청
    //  {
    //      WORD    Type
    //      INT64   AccountNo
    //      WORD    MessageLen
    //      WCHAR   Message[MessageLen / 2]     // null 미포함
    //  }
    //------------------------------------------------------------
    kCsChatReqMessage,

    //------------------------------------------------------------
    // 채팅서버 채팅보내기 응답  (다른 클라가 보낸 채팅도 이걸로 받음)
    //  {
    //      WORD    Type
    //      INT64   AccountNo
    //      WCHAR   ID[20]                      // null 포함
    //      WCHAR   Nickname[20]                // null 포함
    //      WORD    MessageLen
    //      WCHAR   Message[MessageLen / 2]     // null 미포함
    //  }
    //------------------------------------------------------------
    kCsChatResMessage,

    //------------------------------------------------------------
    // 하트비트
    //  {
    //      WORD    Type
    //  }
    //
    // 클라이언트는 이를 30초마다 보내줌.
    // 서버는 40초 이상동안 메시지 수신이 없는 클라이언트를 강제로 끊어줘야 함.
    //------------------------------------------------------------
    kCsChatReqHeartbeat,
};
