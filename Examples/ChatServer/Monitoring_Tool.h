#pragma once
#pragma comment(lib, "Pdh.lib")
#include <Pdh.h>

#define df_PDH_ETHERNET_MAX 8

class Monitoring_Tool {
public:
    struct st_ETHERNET {
        bool _bUse;
        WCHAR _szName[128];

        PDH_HCOUNTER _pdh_Counter_Network_RecvBytes;
        PDH_HCOUNTER _pdh_Counter_Network_SendBytes;
    };
    //생성자, 확인대상 프로세스 핸들, 미입력 시 자기자신
    Monitoring_Tool(HANDLE hProcess = INVALID_HANDLE_VALUE);

    void UpdateCPUTime(void);
    void UpdateQuery(void);
    inline void UpdateAll(void) {
        UpdateCPUTime();
        UpdateQuery();
    }

    float ProcessorTotal(void) { return _fProcessorTotal; }
    float ProcessorUser(void) { return _fProcessorUser; }
    float ProcessorKernel(void) { return _fProcessorKernel; }

    float ProcessTotal(void) { return _fProcessTotal; }
    float ProcessUser(void) { return _fProcessUser; }
    float ProcessKernel(void) { return _fProcessKernel; }

    unsigned long ProcessUserAllocMemory(void) { return _lProcessUserAllocMemory; }
    unsigned long ProcessNonPagedMemory(void) { return _lProcessNonPagedMemory; }

    long AvailableMemory(void) { return _lAvailableMemory; }
    long NonpagedMemory(void) { return _lNonPagedMemory; }

    double NetworkRecvBytes(void) {
        double retval = _pdh_value_Network_RecvBytes;
        _pdh_value_Network_RecvBytes = 0;
        return retval;
    }
    double NetworkSendBytes(void) {
        double retval = _pdh_value_Network_SendBytes;
        _pdh_value_Network_SendBytes = 0;
        return retval;
    }

private:
    HANDLE _hProcess;
    int _iNumberOfProcessors;

    float _fProcessorTotal;
    float _fProcessorUser;
    float _fProcessorKernel;

    float _fProcessTotal;
    float _fProcessUser;
    float _fProcessKernel;

    //CPU 사용률
    ULARGE_INTEGER _ftProcessor_LastKernel;
    ULARGE_INTEGER _ftProcessor_LastUser;
    ULARGE_INTEGER _ftProcessor_LastIdle;

    //프로세스 사용률
    ULARGE_INTEGER _ftProcess_LastKernel;
    ULARGE_INTEGER _ftProcess_LastUser;
    ULARGE_INTEGER _ftProcess_LastTime;

    PDH_HQUERY _Query_PDH;

    //프로세스 유저할당메모리
    PDH_HCOUNTER _Counter_ProcessUserAllocMemory;
    long _lProcessUserAllocMemory;

    //프로세스 논페이지 메모리
    PDH_HCOUNTER _Counter_ProcessNonPagedMemory;
    long _lProcessNonPagedMemory;

    //사용가능 메모리
    PDH_HCOUNTER _Counter_AvailableMemory;
    long _lAvailableMemory;

    //논페이지 메모리
    PDH_HCOUNTER _Counter_NonPagedMemory;
    long _lNonPagedMemory;


    st_ETHERNET _EthernetStruct[df_PDH_ETHERNET_MAX];
    double _pdh_value_Network_RecvBytes;
    double _pdh_value_Network_SendBytes;
};