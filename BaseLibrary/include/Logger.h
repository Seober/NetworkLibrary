#pragma once
#include "CrashDump.h"
#include <atomic>

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

    void Log(const WCHAR* type, LogLevel level, const WCHAR* stringFormat, ...);

    static Logger* GetInstance(void) {
        if (pLogger == nullptr) {
            Lock();
            if (pLogger == nullptr) {
                pLogger = new Logger;
                atexit(Destroy);
            }
            Unlock();
        }
        return pLogger;
    }

    void Crash(void) { Crasher.Crash(); }

    void SetLogLevel(LogLevel level) { MinLevel = level; }


private:
    Logger();

    static void Lock(void) {
        while (InterlockedExchange(&KeySingleton, 1) != 0)
            ;
    }
    static void Unlock(void) { KeySingleton = 0; }

    static void Destroy(void) {
        delete pLogger;
        pLogger = nullptr;
    }


private:
    CrashDump Crasher;

    LogLevel MinLevel;

    std::atomic<LONG64> LogCnt;


    static long KeySingleton;
    static Logger* pLogger;
};