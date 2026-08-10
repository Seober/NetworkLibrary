#pragma once
#include <map>
#include <string>

// INI 설정 파일 로더.
//   [Section]      섹션 시작
//   Key = Value    항목 (앞뒤 공백 무시)
//   # 또는 ;       주석 (줄 전체 또는 값 뒤 인라인)
//
// 모호한 입력은 에러로 중단한다 — 잘못된 설정으로 장시간 측정이 진행되는
// 사고를 막기 위함. 중복 키·형식 오류·빈 값·필수 키 누락을 모두 검출하고,
// 조회되지 않은 키(GetUnusedKeys)로 키 이름 오타까지 잡는다.
//
// 사용 흐름:
//   1. Load / LoadFromString
//   2. Get*Required (필수) / Get* (선택, 기본값 허용) 로 값 조회
//   3. HasError() + GetUnusedKeys() 로 1회 검사 후 진행
//
// 값 범위 검증(예: Trim*2 < Minutes)은 호출부 책임 — 본 클래스는 형식만 담당.
class Config {
public:
    bool Load(const char* path);
    bool LoadFromString(const char* text);

    // 필수 키 — 없거나 형식이 틀리면 에러 기록 (HasError)
    int GetIntRequired(const char* section, const char* key);
    bool GetBoolRequired(const char* section, const char* key);
    std::string GetStringRequired(const char* section, const char* key);

    // 선택 키 — 없으면 defaultValue 사용 + DefaultedKeys에 기록
    int GetInt(const char* section, const char* key, int defaultValue);
    bool GetBool(const char* section, const char* key, bool defaultValue);
    std::string GetString(const char* section, const char* key, const char* defaultValue);

    bool HasError(void) const { return !LastError.empty(); }
    const char* GetLastError(void) const { return LastError.c_str(); }

    // 조회되지 않은 키 목록 — 키 이름 오타 검출용 (빈 문자열이면 정상)
    std::string GetUnusedKeys(void) const;

    // 기본값이 사용된 키 목록 — 측정 이력에 기록해서 나중에 조건 비교 가능하게
    std::string GetDefaultedKeys(void) const { return DefaultedKeys; }

private:
    struct Entry {
        std::string Value;
        int LineNo;
        bool Used;
    };

    void Reset(void);
    bool Fail(int lineNo, const char* message, const std::string& detail);
    void RecordMissing(const char* section, const char* key);
    void RecordDefaulted(const char* section, const char* key);

    // 찾으면 Used 표시 후 반환, 없으면 nullptr
    const Entry* Find(const char* section, const char* key);

private:
    std::map<std::string, std::map<std::string, Entry>> Sections;
    std::string LastError;
    std::string DefaultedKeys;
};
