#include "CrashDump.h"

#pragma comment(lib, "DbgHelp.Lib")
#include <dbghelp.h>
#include <crtdbg.h>
#include <stdio.h>

std::atomic<long> CrashDump::DumpCount{0};

CrashDump::CrashDump() {
    DumpCount = 0;

    _invalid_parameter_handler oldHandler, newHandler;
    newHandler = myInvalidParameterHandler;

    oldHandler = _set_invalid_parameter_handler(newHandler);
    _CrtSetReportMode(_CRT_WARN, 0);
    _CrtSetReportMode(_CRT_ASSERT, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);

    _CrtSetReportHook(_custom_Report_hook);

    // pure virtual function called 에러 핸들러를 사용자 정의 함수로 우회
    _set_purecall_handler(myPurecallHandler);

    SetHandlerDump();
}

void CrashDump::Crash(void) {
    int* p = nullptr;
    *p = 0;
}

LONG WINAPI CrashDump::MyExceptionFilter(__in PEXCEPTION_POINTERS exceptionPointer) {
    SYSTEMTIME stNowTime;

    long dumpCount = ++DumpCount;

    // 현재 프로세스의 메모리 사용량 >> 사용량에 비례해서 덤프파일 사이즈가 나올 것이기 때문에 필요없는 로직
    /*HANDLE hProcess = 0;
	int iWorkingMemory = 0;
	PROCESS_MEMORY_COUNTERS pmc;

	hProcess = GetCurrentProcess();

	if (hProcess == nullptr) return 0;

	if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) iWorkingMemory = (int)(pmc.WorkingSetSize / 1024 / 1024);
	CloseHandle(hProcess);*/

    // 날짜와 시간
    WCHAR filename[MAX_PATH];

    GetLocalTime(&stNowTime);
    wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d.dmp", stNowTime.wYear, stNowTime.wMonth,
             stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, dumpCount);
    wprintf(L"\n\n\n!!! Crash Error !!! %d.%d.%d / %d:%d:%d\n", stNowTime.wYear, stNowTime.wMonth,
            stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);
    wprintf(L"Now Save Dump File...\n");

    HANDLE dumpFile = ::CreateFile(filename, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);

    if (dumpFile != INVALID_HANDLE_VALUE) {
        _MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;

        MinidumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
        MinidumpExceptionInformation.ExceptionPointers = exceptionPointer;
        MinidumpExceptionInformation.ClientPointers = TRUE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                          MiniDumpWithFullMemory, &MinidumpExceptionInformation, nullptr, nullptr);

        CloseHandle(dumpFile);
        wprintf(L"CrashDump Save Finish !");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}