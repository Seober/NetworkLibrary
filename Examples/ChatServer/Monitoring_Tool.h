#pragma once
#pragma comment(lib, "Pdh.lib")
#include <Pdh.h>

class Monitoring_Tool {
public:
    static constexpr int kPdhEthernetMax = 8;

    struct st_ETHERNET {
        bool Use;
        WCHAR Name[128];

        PDH_HCOUNTER pdh_Counter_Network_RecvBytes;
        PDH_HCOUNTER pdh_Counter_Network_SendBytes;
    };
    //생성자, 확인대상 프로세스 핸들, 미입력 시 자기자신
    Monitoring_Tool(HANDLE process = INVALID_HANDLE_VALUE);

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
        double retval = pdh_value_Network_RecvBytes;
        pdh_value_Network_RecvBytes = 0;
        return retval;
    }
    double NetworkSendBytes(void) {
        double retval = pdh_value_Network_SendBytes;
        pdh_value_Network_SendBytes = 0;
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
    ULARGE_INTEGER Processor_LastKernel;
    ULARGE_INTEGER Processor_LastUser;
    ULARGE_INTEGER Processor_LastIdle;

    //프로세스 사용률
    ULARGE_INTEGER Process_LastKernel;
    ULARGE_INTEGER Process_LastUser;
    ULARGE_INTEGER Process_LastTime;

    PDH_HQUERY Query_PDH;

    //프로세스 유저할당메모리
    PDH_HCOUNTER Counter_ProcessUserAllocMemory;
    long ProcessUserAllocMemory_;

    //프로세스 논페이지 메모리
    PDH_HCOUNTER Counter_ProcessNonPagedMemory;
    long ProcessNonPagedMemory_;

    //사용가능 메모리
    PDH_HCOUNTER Counter_AvailableMemory;
    long AvailableMemory_;

    //논페이지 메모리
    PDH_HCOUNTER Counter_NonPagedMemory;
    long NonPagedMemory_;


    st_ETHERNET EthernetStruct[kPdhEthernetMax];
    double pdh_value_Network_RecvBytes;
    double pdh_value_Network_SendBytes;
};