#include "catch_amalgamated.hpp"
#include "Config.h"

// Config(INI 로더) 단위 테스트.
// 대부분 LoadFromString으로 검증 — 파일 생성·정리가 없어 빠르고 경로 문제도 없음.
// 파일 경로 처리는 마지막 케이스에서만 Load로 확인.

TEST_CASE("Config: basic parse of int, bool, string", "[config][parse]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = 12001\n"
                                  "NagleEnabled = false\n"
                                  "Name = TestServer\n"));

    REQUIRE(config.GetIntRequired("Server", "Port") == 12001);
    REQUIRE(config.GetBoolRequired("Server", "NagleEnabled") == false);
    REQUIRE(config.GetStringRequired("Server", "Name") == "TestServer");
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: skips UTF-8 BOM", "[config][parse]") {
    Config config;
    REQUIRE(config.LoadFromString("\xEF\xBB\xBF[Run]\n"
                                  "Label = baseline\n"));

    REQUIRE(config.GetStringRequired("Run", "Label") == "baseline");
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: ignores comment lines and blank lines", "[config][parse]") {
    Config config;
    REQUIRE(config.LoadFromString("# hash comment\n"
                                  "; semicolon comment\n"
                                  "\n"
                                  "[Server]\n"
                                  "\n"
                                  "Port = 12001\n"
                                  "; trailing comment line\n"));

    REQUIRE(config.GetIntRequired("Server", "Port") == 12001);
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: strips inline comment and surrounding whitespace", "[config][parse]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "   Port   =   12001   ; inline comment\n"
                                  "MaxUser = 32000 # hash inline\n"
                                  "\tName\t=\tEchoServer\t\n"));

    REQUIRE(config.GetIntRequired("Server", "Port") == 12001);
    REQUIRE(config.GetIntRequired("Server", "MaxUser") == 32000);
    REQUIRE(config.GetStringRequired("Server", "Name") == "EchoServer");
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: optional key falls back to default and is recorded", "[config][default]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = 12001\n"));

    REQUIRE(config.GetInt("Server", "WorkerThreadCnt", 4) == 4);
    REQUIRE(config.HasError() == false);
    REQUIRE(config.GetDefaultedKeys().find("WorkerThreadCnt") != std::string::npos);
}

TEST_CASE("Config: missing section falls back to default", "[config][default]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = 12001\n"));

    REQUIRE(config.GetInt("Client", "ClientCount", 100) == 100);
    REQUIRE(config.GetString("Client", "ServerIP", "127.0.0.1") == "127.0.0.1");
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: duplicate key fails with line number", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = 12001\n"
                                  "Port = 12002\n") == false);

    std::string error = config.GetLastError();
    REQUIRE(error.find("duplicate key") != std::string::npos);
    REQUIRE(error.find("line 3") != std::string::npos);
}

TEST_CASE("Config: non-integer value is an error and yields default", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = abc\n"
                                  "MaxUser = 12abc\n"));

    REQUIRE(config.GetInt("Server", "Port", 12001) == 12001);
    REQUIRE(config.GetInt("Server", "MaxUser", 32000) == 32000);
    REQUIRE(config.HasError());
}

TEST_CASE("Config: boolean accepts true/false/1/0 case-insensitively", "[config][parse]") {
    Config config;
    REQUIRE(config.LoadFromString("[Flags]\n"
                                  "A = true\n"
                                  "B = FALSE\n"
                                  "C = 1\n"
                                  "D = 0\n"
                                  "E = TrUe\n"));

    REQUIRE(config.GetBoolRequired("Flags", "A") == true);
    REQUIRE(config.GetBoolRequired("Flags", "B") == false);
    REQUIRE(config.GetBoolRequired("Flags", "C") == true);
    REQUIRE(config.GetBoolRequired("Flags", "D") == false);
    REQUIRE(config.GetBoolRequired("Flags", "E") == true);
    REQUIRE(config.HasError() == false);
}

TEST_CASE("Config: non-boolean value is an error", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Flags]\n"
                                  "A = yes\n"));

    REQUIRE(config.GetBool("Flags", "A", false) == false);
    REQUIRE(config.HasError());
}

TEST_CASE("Config: malformed lines fail to load", "[config][error]") {
    SECTION("key outside section") {
        Config config;
        REQUIRE(config.LoadFromString("Port = 12001\n") == false);
        REQUIRE(std::string(config.GetLastError()).find("outside section") != std::string::npos);
    }
    SECTION("line without '='") {
        Config config;
        REQUIRE(config.LoadFromString("[Server]\n"
                                      "Port 12001\n") == false);
        REQUIRE(std::string(config.GetLastError()).find("missing '='") != std::string::npos);
    }
    SECTION("section not closed") {
        Config config;
        REQUIRE(config.LoadFromString("[Server\n"
                                      "Port = 12001\n") == false);
        REQUIRE(std::string(config.GetLastError()).find("not closed") != std::string::npos);
    }
}

TEST_CASE("Config: missing required key is an error", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port = 12001\n"));

    REQUIRE(config.GetIntRequired("Server", "MaxUser") == 0);
    REQUIRE(config.HasError());
    REQUIRE(std::string(config.GetLastError()).find("MaxUser") != std::string::npos);
}

TEST_CASE("Config: unused key is reported (typo detection)", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Client]\n"
                                  "ClientCount = 2000\n"
                                  "ClinetCount = 500\n"));

    REQUIRE(config.GetIntRequired("Client", "ClientCount") == 2000);

    std::string unused = config.GetUnusedKeys();
    REQUIRE(unused.find("ClinetCount") != std::string::npos);
    REQUIRE(unused.find("ClientCount ") == std::string::npos);   // 조회된 키는 제외
}

TEST_CASE("Config: empty value fails to load", "[config][error]") {
    Config config;
    REQUIRE(config.LoadFromString("[Server]\n"
                                  "Port =\n") == false);
    REQUIRE(std::string(config.GetLastError()).find("empty value") != std::string::npos);
}

TEST_CASE("Config: missing file fails to load", "[config][error]") {
    Config config;
    REQUIRE(config.Load("no_such_config_file_for_unit_test.ini") == false);
    REQUIRE(std::string(config.GetLastError()).find("cannot open") != std::string::npos);
}
