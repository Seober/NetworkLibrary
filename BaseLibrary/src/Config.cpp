#include "Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

void Trim(std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        text.clear();
        return;
    }
    std::size_t end = text.find_last_not_of(" \t");
    text = text.substr(begin, end - begin + 1);
}

// 값 뒤 인라인 주석 제거 — 첫 ';' 또는 '#' 이후를 잘라낸다.
// 따라서 값 자체에는 이 두 문자를 쓸 수 없다 (문서에 명시).
void StripInlineComment(std::string& text) {
    std::size_t pos = text.find_first_of(";#");
    if (pos != std::string::npos)
        text.erase(pos);
}

bool ParseInt(const std::string& text, int& out) {
    if (text.empty())
        return false;
    char* end = nullptr;
    long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0')   // 전체가 정수여야 함 ("12abc" 거부)
        return false;
    out = (int)parsed;
    return true;
}

bool ParseBool(const std::string& text, bool& out) {
    std::string lower = text;
    for (std::size_t i = 0; i < lower.size(); i++) {
        if (lower[i] >= 'A' && lower[i] <= 'Z')
            lower[i] = (char)(lower[i] - 'A' + 'a');
    }
    if (lower == "true" || lower == "1") {
        out = true;
        return true;
    }
    if (lower == "false" || lower == "0") {
        out = false;
        return true;
    }
    return false;
}

}   // namespace


void Config::Reset(void) {
    Sections.clear();
    LastError.clear();
    DefaultedKeys.clear();
}

bool Config::Fail(int lineNo, const char* message, const std::string& detail) {
    if (!LastError.empty())
        LastError += " | ";
    if (lineNo > 0)
        LastError += "line " + std::to_string(lineNo) + ": ";
    LastError += message;
    if (!detail.empty())
        LastError += " (" + detail + ")";
    return false;
}

void Config::RecordMissing(const char* section, const char* key) {
    if (!LastError.empty())
        LastError += " | ";
    LastError += std::string("missing required key: [") + section + "] " + key;
}

void Config::RecordDefaulted(const char* section, const char* key) {
    if (!DefaultedKeys.empty())
        DefaultedKeys += ", ";
    DefaultedKeys += std::string("[") + section + "] " + key;
}


bool Config::Load(const char* path) {
    Reset();
    if (path == nullptr)
        return Fail(0, "null config path", "");

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return Fail(0, "cannot open config file", path);

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    return LoadFromString(text.c_str());
}

bool Config::LoadFromString(const char* text) {
    Reset();
    if (text == nullptr)
        return Fail(0, "null config text", "");

    // UTF-8 BOM 건너뜀 — 본 repo 표준이 UTF-8 with BOM이라 필수
    const char* cursor = text;
    if ((unsigned char)cursor[0] == 0xEF && (unsigned char)cursor[1] == 0xBB &&
        (unsigned char)cursor[2] == 0xBF)
        cursor += 3;

    std::istringstream stream(cursor);
    std::string line;
    std::string current;
    bool hasSection = false;
    int lineNo = 0;

    while (std::getline(stream, line)) {
        lineNo++;

        if (!line.empty() && line.back() == '\r')   // CRLF 처리
            line.pop_back();

        Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            std::size_t close = line.find(']');
            if (close == std::string::npos)
                return Fail(lineNo, "section not closed", line);

            std::string name = line.substr(1, close - 1);
            Trim(name);
            if (name.empty())
                return Fail(lineNo, "empty section name", line);

            current = name;   // 같은 섹션 재등장 시 병합 (중복 키는 아래에서 차단)
            hasSection = true;
            continue;
        }

        std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            return Fail(lineNo, "missing '='", line);
        if (!hasSection)
            return Fail(lineNo, "key outside section", line);

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        Trim(key);
        StripInlineComment(value);
        Trim(value);

        if (key.empty())
            return Fail(lineNo, "empty key", line);
        if (value.empty())   // 명시적으로 비운 값은 오타 가능성이 높아 에러 처리
            return Fail(lineNo, "empty value", line);

        std::map<std::string, Entry>& table = Sections[current];
        if (table.find(key) != table.end())
            return Fail(lineNo, "duplicate key", key);

        Entry entry;
        entry.Value = value;
        entry.LineNo = lineNo;
        entry.Used = false;
        table[key] = entry;
    }

    return true;
}


const Config::Entry* Config::Find(const char* section, const char* key) {
    auto sectionIter = Sections.find(section);
    if (sectionIter == Sections.end())
        return nullptr;

    auto keyIter = sectionIter->second.find(key);
    if (keyIter == sectionIter->second.end())
        return nullptr;

    keyIter->second.Used = true;
    return &keyIter->second;
}


int Config::GetIntRequired(const char* section, const char* key) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordMissing(section, key);
        return 0;
    }
    int value = 0;
    if (!ParseInt(entry->Value, value)) {
        Fail(entry->LineNo, "not an integer", entry->Value);
        return 0;
    }
    return value;
}

bool Config::GetBoolRequired(const char* section, const char* key) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordMissing(section, key);
        return false;
    }
    bool value = false;
    if (!ParseBool(entry->Value, value)) {
        Fail(entry->LineNo, "not a boolean (true/false/1/0)", entry->Value);
        return false;
    }
    return value;
}

std::string Config::GetStringRequired(const char* section, const char* key) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordMissing(section, key);
        return std::string();
    }
    return entry->Value;
}


int Config::GetInt(const char* section, const char* key, int defaultValue) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordDefaulted(section, key);
        return defaultValue;
    }
    int value = 0;
    if (!ParseInt(entry->Value, value)) {
        Fail(entry->LineNo, "not an integer", entry->Value);
        return defaultValue;
    }
    return value;
}

bool Config::GetBool(const char* section, const char* key, bool defaultValue) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordDefaulted(section, key);
        return defaultValue;
    }
    bool value = false;
    if (!ParseBool(entry->Value, value)) {
        Fail(entry->LineNo, "not a boolean (true/false/1/0)", entry->Value);
        return defaultValue;
    }
    return value;
}

std::string Config::GetString(const char* section, const char* key, const char* defaultValue) {
    const Entry* entry = Find(section, key);
    if (entry == nullptr) {
        RecordDefaulted(section, key);
        return std::string(defaultValue);
    }
    return entry->Value;
}


std::string Config::GetUnusedKeys(void) const {
    std::string result;
    for (const auto& section : Sections) {
        for (const auto& entry : section.second) {
            if (entry.second.Used)
                continue;
            if (!result.empty())
                result += ", ";
            result += "[" + section.first + "] " + entry.first + " (line " +
                      std::to_string(entry.second.LineNo) + ")";
        }
    }
    return result;
}
