#include "Logger.h"
#include <strsafe.h>

long Logger::KeySingleton = 0;
Logger* Logger::pLogger = nullptr;


Logger::Logger() {
    MinLevel = LogLevel::kDebug;
    LogCnt = 0;
}


void Logger::Log(const WCHAR* type, LogLevel level, const WCHAR* stringFormat, ...) {
    if (MinLevel <= level) {
        LONG64 logCnt = InterlockedIncrement64(&LogCnt);

        int retval_FileName;
        int retval_LogTag;
        int retval_LogMessage;

        WCHAR fileName[kBufsizeFilename];
        WCHAR logTag[kBufsizeLogTag];
        WCHAR logMessage[kBufsizeLogMessage];

        //Setting Log_FileName
        SYSTEMTIME stNowTime;
        GetLocalTime(&stNowTime);
        retval_FileName = StringCchPrintf(fileName, kBufsizeFilename, L"%hd%02hd_%s.txt",
                                          stNowTime.wYear, stNowTime.wMonth, type);

        //Setting Log_Tag
        switch (level) {
            case LogLevel::kDebug:
                retval_LogTag = StringCchPrintf(
                    logTag, kBufsizeLogTag,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / DEBUG] [%08I64d] ", type,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, logCnt);
                break;

            case LogLevel::kError:
                retval_LogTag = StringCchPrintf(
                    logTag, kBufsizeLogTag,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / ERROR] [%08I64d] ", type,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, logCnt);
                break;

            case LogLevel::kSystem:
                retval_LogTag = StringCchPrintf(
                    logTag, kBufsizeLogTag,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / SYSTEM] [%08I64d] ", type,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, logCnt);
                break;
            default:
                retval_LogTag = StringCchPrintf(
                    logTag, kBufsizeLogTag,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / OutOfRange] [%08I64d] ", type,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, logCnt);
                break;
        }

        //Setting Log_Message
        va_list va;
        va_start(va, stringFormat);
        retval_LogMessage =
            StringCchVPrintf(logMessage, kBufsizeLogMessage, stringFormat, va);
        va_end(va);


        //WriteFile
        // _wfopen_s는 sharing-deny 모드(MSVC 공식)라 한 시점에 한 스레드만 파일을 잡을 수 있음.
        // do-while 재시도 = OS file lock을 활용한 자연스러운 스핀락 (별도 mutex 불필요, 다중 fwrite 줄 섞임 방지).
        FILE* file = nullptr;
        do {
            _wfopen_s(&file, fileName, L"ab");
        } while (file == nullptr);

        fwprintf_s(file, logTag);
        fwprintf_s(file, logMessage);

        if (retval_FileName)
            fwprintf_s(file, L" # FileName Err:0x%x #", retval_FileName);
        if (retval_LogTag)
            fwprintf_s(file, L" # LogTag Err:0x%x #", retval_LogTag);
        if (retval_LogMessage)
            fwprintf_s(file, L" # LogMessage Err:0x%x #", retval_LogMessage);

        fclose(file);
    }
}