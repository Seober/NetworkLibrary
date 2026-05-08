#pragma once
#include "CrashDump.h"

class Logger {
public:
    static constexpr int kBufsizeFilename = 64;
    static constexpr int kBufsizeLogTag = 128;
    static constexpr int kBufsizeLogMessage = 256;

    enum class LogLevel {
        kDebug,
        kError,
        kSystem
    };

    void Log(const WCHAR* szType, LogLevel level, const WCHAR* szStringFormat, ...);

    static Logger* GetInstance(void) {
        if (pLogger == NULL) {
            Lock();
            if (pLogger == NULL) {
                pLogger = new Logger;
                atexit(Destroy);
            }
            Unlock();
        }
        return pLogger;
    }

    void Crash(void) { Crasher.Crash(); }

    void SetLogLevel(LogLevel level) { Log_Level = level; }


private:
    Logger();

    static void Lock(void) {
        while (InterlockedExchange(&Key_Singleton, 1) != 0)
            ;
    }
    static void Unlock(void) { Key_Singleton = 0; }

    static void Destroy(void) {
        delete pLogger;
        pLogger = NULL;
    }


private:
    CrashDump Crasher;

    LogLevel Log_Level;

    unsigned __int64 LogCnt_;


    static long Key_Singleton;
    static Logger* pLogger;
};