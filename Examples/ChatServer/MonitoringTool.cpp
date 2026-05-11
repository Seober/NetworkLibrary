#include <windows.h>
#include <strsafe.h>

#include "MonitoringTool.h"

MonitoringTool::MonitoringTool(HANDLE process) {
    if (process == INVALID_HANDLE_VALUE)
        Process = GetCurrentProcess();
    WCHAR processNameBuf[1024];
    DWORD bufSize = sizeof(processNameBuf) / 2;
    QueryFullProcessImageNameW(Process, 0, processNameBuf, &bufSize);

    WCHAR* context;
    WCHAR* befPtr;
    WCHAR* ptr = wcstok_s(processNameBuf, L"\\", &context);
    do {
        befPtr = ptr;
        ptr = wcstok_s(NULL, L"\\", &context);
    } while (ptr != NULL);

    ptr = befPtr;

    while (1) {
        if (*befPtr == L'.') {
            if (!wcscmp(befPtr, L".exe")) {
                *befPtr = L'\0';
                break;
            }
        }

        befPtr++;
    }

    // 프로세서 갯수, 프로세스 실행률 계산 시 CPU(프로세서) 갯수를 나눠 실제 사용률을 구함
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    NumberOfProcessors = systemInfo.dwNumberOfProcessors;

    ProcessorTotal_ = 0;
    ProcessorUser_ = 0;
    ProcessorKernel_ = 0;

    ProcessTotal_ = 0;
    ProcessUser_ = 0;
    ProcessKernel_ = 0;

    Processor_LastKernel.QuadPart = 0;
    Processor_LastUser.QuadPart = 0;
    Processor_LastIdle.QuadPart = 0;

    Process_LastUser.QuadPart = 0;
    Process_LastKernel.QuadPart = 0;
    Process_LastTime.QuadPart = 0;

    PdhOpenQuery(NULL, NULL, &Query_PDH);

    WCHAR strBuf[1024];
    wsprintf(strBuf, L"\\Process(%s)\\Private Bytes", ptr);
    PdhAddCounter(Query_PDH, strBuf, NULL, &Counter_ProcessUserAllocMemory);

    wsprintf(strBuf, L"\\Process(%s)\\Pool Nonpaged Bytes", ptr);
    PdhAddCounter(Query_PDH, strBuf, NULL, &Counter_ProcessNonPagedMemory);

    PdhAddCounter(Query_PDH, L"\\Memory\\Available MBytes", NULL, &Counter_AvailableMemory);
    PdhAddCounter(Query_PDH, L"\\Memory\\Pool Nonpaged Bytes", NULL, &Counter_NonPagedMemory);

    /*
	쿼리 추가필요 시 https://docs.microsoft.com/ko-kr/windows/win32/perfctrs/browsing-performance-counters를 통해 쿼리를 얻고 하드코딩
	*/


    //네트워크 트래픽
    int cnt = 0;
    bool err = false;
    WCHAR* cur = NULL;
    WCHAR* counters = NULL;
    WCHAR* interfaces = NULL;
    DWORD counterSize = 0, interfaceSize = 0;
    WCHAR query[1024] = {
        0,
    };

    // 먼저 버퍼길이를 얻기 위해 Out BUffer 파라미터를 NULL을 넣어 사이즈만 확인
    PdhEnumObjectItems(NULL, NULL, L"Network Interface", counters, &counterSize, interfaces,
                       &interfaceSize, PERF_DETAIL_WIZARD, 0);
    counters = new WCHAR[counterSize];
    interfaces = new WCHAR[interfaceSize];

    if (PdhEnumObjectItems(NULL, NULL, L"Network Interface", counters, &counterSize,
                           interfaces, &interfaceSize, PERF_DETAIL_WIZARD,
                           0) != ERROR_SUCCESS) {
        delete[] counters;
        delete[] interfaces;
        return;
    }


    cnt = 0;
    cur = interfaces;

    for (; *cur != L'\0' && cnt < kPdhEthernetMax; cur += wcslen(cur) + 1, cnt++) {
        EthernetStruct[cnt].Use = true;
        EthernetStruct[cnt].Name[0] = L'\0';
        wcscpy_s(EthernetStruct[cnt].Name, cur);
        query[0] = L'\0';
        StringCbPrintf(query, sizeof(WCHAR) * 1024,
                       L"\\Network Interface(%s)\\Bytes Received/sec", cur);
        PdhAddCounter(Query_PDH, query, NULL,
                      &EthernetStruct[cnt].pdh_Counter_Network_RecvBytes);
        query[0] = L'\0';
        StringCbPrintf(query, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Sent/sec",
                       cur);
        PdhAddCounter(Query_PDH, query, NULL,
                      &EthernetStruct[cnt].pdh_Counter_Network_SendBytes);
    }


    UpdateCPUTime();
    UpdateQuery();
}

//CPU 사용률 갱신, 500ms ~ 1s 단위 호출이 적절
void MonitoringTool::UpdateCPUTime() {
    //포로세서 사용률 갱신
    //본래 사용 구조체 : FILETIME >> 100 ns 단위의 시간단위를 표현하는 구조체
    //	ㄴ ULARGE_INTEGER와 구조가 같으므로 이를 사용

    ULARGE_INTEGER idle;
    ULARGE_INTEGER kernel;
    ULARGE_INTEGER user;

    // 시스템 사용시간 획득
    // 아이들타임, 커널사용타임(아이들포함), 유저사용타임
    if (GetSystemTimes((PFILETIME)&idle, (PFILETIME)&kernel, (PFILETIME)&user) == false)
        return;

    ULONGLONG kernelDiff = kernel.QuadPart - Processor_LastKernel.QuadPart;
    ULONGLONG userDiff = user.QuadPart - Processor_LastUser.QuadPart;
    ULONGLONG idleDiff = idle.QuadPart - Processor_LastIdle.QuadPart;

    ULONGLONG total = kernelDiff + userDiff;
    ULONGLONG timeDiff;

    if (total == 0) {
        ProcessorUser_ = 0.0f;
        ProcessorKernel_ = 0.0f;
        ProcessorTotal_ = 0.0f;
    } else {
        //커널에 아이들이 포함되어있으므로 빼서 계산
        ProcessorTotal_ = (float)((double)(total - idleDiff) / total * 100.0f);
        ProcessorUser_ = (float)((double)userDiff / total * 100.0f);
        ProcessorKernel_ = (float)((double)(kernelDiff - idleDiff) / total * 100.0f);
    }

    Processor_LastKernel = kernel;
    Processor_LastUser = user;
    Processor_LastIdle = idle;


    // 지정된 프로세스 사용률 갱신

    ULARGE_INTEGER none;
    ULARGE_INTEGER nowTime;

    // 현재 100ns 단위 시간을 구함(UTC 시간) ... UTC 시간 : 세계 협정 시간, 한국은 UTC + 9

    // a = 샘플간격의 시스템 시간 (실제 경과시간)
    // b = 프로세스의 CPU 사용시간
    // a : 100 = b : 사용률


    //경과시간 측정
    GetSystemTimeAsFileTime((LPFILETIME)&nowTime);

    //프로세스 사용시간 측정
    // 두,세번째 파라미터는 프로세스생성시간과 프로세스 종료시간으로 미사용할것, 종료되지않은경우 값이없음
    GetProcessTimes(Process, (LPFILETIME)&none, (LPFILETIME)&none, (LPFILETIME)&kernel,
                    (LPFILETIME)&user);


    //이전에 저장한 프로세스 시간과의 차를 구해 실제로 얼마의 시간이 지났는지 확인
    // 그리고 실제 지나온 시간으로 나누면 사용률이 나옴
    timeDiff = nowTime.QuadPart - Process_LastTime.QuadPart;
    userDiff = user.QuadPart - Process_LastUser.QuadPart;
    kernelDiff = kernel.QuadPart - Process_LastKernel.QuadPart;

    total = kernelDiff + userDiff;

    ProcessTotal_ = (float)(total / (double)NumberOfProcessors / (double)timeDiff * 100.0f);
    ProcessKernel_ =
        (float)(kernelDiff / (double)NumberOfProcessors / (double)timeDiff * 100.0f);
    ProcessUser_ = (float)(userDiff / (double)NumberOfProcessors / (double)timeDiff * 100.0f);

    Process_LastTime = nowTime;
    Process_LastKernel = kernel;
    Process_LastUser = user;
}


void MonitoringTool::UpdateQuery() {
    PdhCollectQueryData(Query_PDH);

    PDH_FMT_COUNTERVALUE counterVal;

    PdhGetFormattedCounterValue(Counter_ProcessUserAllocMemory, PDH_FMT_LONG, NULL, &counterVal);
    ProcessUserAllocMemory_ = counterVal.longValue;

    PdhGetFormattedCounterValue(Counter_ProcessNonPagedMemory, PDH_FMT_LONG, NULL, &counterVal);
    ProcessNonPagedMemory_ = counterVal.longValue;

    PdhGetFormattedCounterValue(Counter_AvailableMemory, PDH_FMT_LONG, NULL, &counterVal);
    AvailableMemory_ = counterVal.longValue;

    PdhGetFormattedCounterValue(Counter_NonPagedMemory, PDH_FMT_LONG, NULL, &counterVal);
    NonPagedMemory_ = counterVal.longValue;

    /////////////////////////////////////////////
    for (int cnt = 0; cnt < kPdhEthernetMax; cnt++) {
        if (EthernetStruct[cnt].Use) {
            if (PdhGetFormattedCounterValue(EthernetStruct[cnt].pdh_Counter_Network_RecvBytes,
                                            PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS)
                pdh_value_Network_RecvBytes += counterVal.doubleValue;
            if (PdhGetFormattedCounterValue(EthernetStruct[cnt].pdh_Counter_Network_SendBytes,
                                            PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS)
                pdh_value_Network_SendBytes += counterVal.doubleValue;
        }
    }
}