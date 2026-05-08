#include "Logger.h"
#include <strsafe.h>

long Logger::Key_Singleton = 0;
Logger* Logger::pLogger = NULL;


Logger::Logger() {
    Log_Level = LogLevel::kDebug;
    _LogCnt = 0;
}


void Logger::Log(const WCHAR* szType, LogLevel level, const WCHAR* szStringFormat, ...) {
    if (Log_Level <= level) {
        unsigned __int64 LogCnt = InterlockedIncrement(&_LogCnt);

        int retval_FileName;
        int retval_LogTag;
        int retval_LogMessage;

        WCHAR szFileName[df_LOG_BUFSIZE_FILENAME];
        WCHAR szLogTag[df_LOG_BUFSIZE_LOGTAG];
        WCHAR szLogMessage[df_LOG_BUFSIZE_LOGMESSAGE];

        //Setting Log_FileName
        SYSTEMTIME stNowTime;
        GetLocalTime(&stNowTime);
        retval_FileName = StringCchPrintf(szFileName, df_LOG_BUFSIZE_FILENAME, L"%hd%02hd_%s.txt",
                                          stNowTime.wYear, stNowTime.wMonth, szType);

        //Setting Log_Tag
        switch (level) {
            case LogLevel::kDebug:
                retval_LogTag = StringCchPrintf(
                    szLogTag, df_LOG_BUFSIZE_LOGTAG,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / DEBUG] [%08I64u] ", szType,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, LogCnt);
                break;

            case LogLevel::kError:
                retval_LogTag = StringCchPrintf(
                    szLogTag, df_LOG_BUFSIZE_LOGTAG,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / ERROR] [%08I64u] ", szType,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, LogCnt);
                break;

            case LogLevel::kSystem:
                retval_LogTag = StringCchPrintf(
                    szLogTag, df_LOG_BUFSIZE_LOGTAG,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / SYSTEM] [%08I64u] ", szType,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, LogCnt);
                break;
            default:
                retval_LogTag = StringCchPrintf(
                    szLogTag, df_LOG_BUFSIZE_LOGTAG,
                    L"\n[%s] [%hd-%02hd-%02hd %02hd:%02hd:%02hd / OutOfRange] [%08I64u] ", szType,
                    stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour,
                    stNowTime.wMinute, stNowTime.wSecond, LogCnt);
                break;
        }

        //Setting Log_Message
        va_list va;
        va_start(va, szStringFormat);
        retval_LogMessage =
            StringCchVPrintf(szLogMessage, df_LOG_BUFSIZE_LOGMESSAGE, szStringFormat, va);
        va_end(va);


        //WriteFile
        // _wfopen_s는 sharing-deny 모드(MSVC 공식)라 한 시점에 한 스레드만 파일을 잡을 수 있음.
        // do-while 재시도 = OS file lock을 활용한 자연스러운 스핀락 (별도 mutex 불필요, 다중 fwrite 줄 섞임 방지).
        FILE* pFile = NULL;
        do {
            _wfopen_s(&pFile, szFileName, L"ab");
        } while (pFile == NULL);

        fwprintf_s(pFile, szLogTag);
        fwprintf_s(pFile, szLogMessage);

        if (retval_FileName)
            fwprintf_s(pFile, L" # FileName Err:0x%x #", retval_FileName);
        if (retval_LogTag)
            fwprintf_s(pFile, L" # LogTag Err:0x%x #", retval_LogTag);
        if (retval_LogMessage)
            fwprintf_s(pFile, L" # LogMessage Err:0x%x #", retval_LogMessage);

        fclose(pFile);
    }
}