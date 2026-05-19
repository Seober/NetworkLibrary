#pragma once

//#ifndef CRASH_DUMP
//#define CRASH_DUMP

#include <windows.h>
#include <atomic>

class CrashDump {
public:
    CrashDump();

    static void Crash(void);  // @

    static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS exceptionPointer);  //@

    static void SetHandlerDump() { SetUnhandledExceptionFilter(MyExceptionFilter); }
    static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function,
                                          const wchar_t* file, unsigned int line,
                                          uintptr_t reserved) {
        Crash();
    }

    static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue) {
        Crash();
        return true;
    }

    static void myPurecallHandler(void) { Crash(); }

    static std::atomic<long> DumpCount;
};


//#endif