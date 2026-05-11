#pragma once
#pragma comment(lib, "Pdh.lib")
#include <Pdh.h>

class MonitoringTool {
public:
    static constexpr int kPdhEthernetMax = 8;

    struct Ethernet {
        bool Use;
        WCHAR Name[128];

        PDH_HCOUNTER PDHCounterNetworkRecvBytes;
        PDH_HCOUNTER PDHCounterNetworkSendBytes;
    };
    //생성자, 확인대상 프로세스 핸들, 미입력 시 자기자신
    MonitoringTool(HANDLE process = INVALID_HANDLE_VALUE);

    void UpdateCPUTime(void);
    void UpdateQuery(void);
    inline void UpdateAll(void) {
        UpdateCPUTime();
        UpdateQuery();
    }

    float ProcessorTotal(void) { return ProcessorTotal_; }
    float ProcessorUser(void) { return ProcessorUser_; }
    float ProcessorKernel(void) { return ProcessorKernel_; }

    float ProcessTotal(void) { return ProcessTotal_; }
    float ProcessUser(void) { return ProcessUser_; }
    float ProcessKernel(void) { return ProcessKernel_; }

    unsigned long ProcessUserAllocMemory(void) { return ProcessUserAllocMemory_; }
    unsigned long ProcessNonPagedMemory(void) { return ProcessNonPagedMemory_; }

    long AvailableMemory(void) { return AvailableMemory_; }
    long NonpagedMemory(void) { return NonPagedMemory_; }

    double NetworkRecvBytes(void) {
        double retval = PDHValueNetworkRecvBytes;
        PDHValueNetworkRecvBytes = 0;
        return retval;
    }
    double NetworkSendBytes(void) {
        double retval = PDHValueNetworkSendBytes;
        PDHValueNetworkSendBytes = 0;
        return retval;
    }

private:
    HANDLE Process;
    int NumberOfProcessors;

    float ProcessorTotal_;
    float ProcessorUser_;
    float ProcessorKernel_;

    float ProcessTotal_;
    float ProcessUser_;
    float ProcessKernel_;

    //CPU 사용률
    ULARGE_INTEGER ProcessorLastKernel;
    ULARGE_INTEGER ProcessorLastUser;
    ULARGE_INTEGER ProcessorLastIdle;

    //프로세스 사용률
    ULARGE_INTEGER ProcessLastKernel;
    ULARGE_INTEGER ProcessLastUser;
    ULARGE_INTEGER ProcessLastTime;

    PDH_HQUERY QueryPDH;

    //프로세스 유저할당메모리
    PDH_HCOUNTER CounterProcessUserAllocMemory;
    long ProcessUserAllocMemory_;

    //프로세스 논페이지 메모리
    PDH_HCOUNTER CounterProcessNonPagedMemory;
    long ProcessNonPagedMemory_;

    //사용가능 메모리
    PDH_HCOUNTER CounterAvailableMemory;
    long AvailableMemory_;

    //논페이지 메모리
    PDH_HCOUNTER CounterNonPagedMemory;
    long NonPagedMemory_;


    Ethernet EthernetStruct[kPdhEthernetMax];
    double PDHValueNetworkRecvBytes;
    double PDHValueNetworkSendBytes;
};