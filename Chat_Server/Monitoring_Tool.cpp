#include <windows.h>
#include <strsafe.h>

#include "Monitoring_Tool.h"

Monitoring_Tool::Monitoring_Tool(HANDLE hProcess)
{
	if (hProcess == INVALID_HANDLE_VALUE) _hProcess = GetCurrentProcess();
	WCHAR ProcessNameBuf[1024];
	DWORD BufSize = sizeof(ProcessNameBuf) / 2;
	QueryFullProcessImageNameW(_hProcess, 0, ProcessNameBuf, &BufSize);

	WCHAR* pContext;
	WCHAR* befPtr;
	WCHAR* ptr = wcstok_s(ProcessNameBuf, L"\\", &pContext);
	do
	{
		befPtr = ptr;
		ptr = wcstok_s(NULL, L"\\", &pContext);
	} while (ptr != NULL);

	ptr = befPtr;

	while (1)
	{
		if (*befPtr == L'.')
		{
			if (!wcscmp(befPtr, L".exe"))
			{
				*befPtr = L'\0';
				break;
			}
		}

		befPtr++;
	}

	// 프로세서 갯수, 프로세스 실행률 계산 시 CPU(프로세서) 갯수를 나눠 실제 사용률을 구함
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	_iNumberOfProcessors = SystemInfo.dwNumberOfProcessors;

	_fProcessorTotal = 0;
	_fProcessorUser = 0;
	_fProcessorKernel = 0;

	_fProcessTotal = 0;
	_fProcessUser = 0;
	_fProcessKernel = 0;

	_ftProcessor_LastKernel.QuadPart = 0;
	_ftProcessor_LastUser.QuadPart = 0;
	_ftProcessor_LastIdle.QuadPart = 0;

	_ftProcess_LastUser.QuadPart = 0;
	_ftProcess_LastKernel.QuadPart = 0;
	_ftProcess_LastTime.QuadPart = 0;

	PdhOpenQuery(NULL, NULL, &_Query_PDH);

	WCHAR StrBuf[1024];
	wsprintf(StrBuf, L"\\Process(%s)\\Private Bytes", ptr);
	PdhAddCounter(_Query_PDH, StrBuf, NULL, &_Counter_ProcessUserAllocMemory);

	wsprintf(StrBuf, L"\\Process(%s)\\Pool Nonpaged Bytes", ptr);
	PdhAddCounter(_Query_PDH, StrBuf, NULL, &_Counter_ProcessNonPagedMemory);

	PdhAddCounter(_Query_PDH, L"\\Memory\\Available MBytes", NULL, &_Counter_AvailableMemory);
	PdhAddCounter(_Query_PDH, L"\\Memory\\Pool Nonpaged Bytes", NULL, &_Counter_NonPagedMemory);

	/*
	쿼리 추가필요 시 https://docs.microsoft.com/ko-kr/windows/win32/perfctrs/browsing-performance-counters를 통해 쿼리를 얻고 하드코딩
	*/


	//네트워크 트래픽
	int iCnt = 0;
	bool bErr = false;
	WCHAR* szCur = NULL;
	WCHAR* szCounters = NULL;
	WCHAR* szInterfaces = NULL;
	DWORD dwCounterSize = 0, dwInterfaceSize = 0;
	WCHAR szQuery[1024] = { 0, };

	// 먼저 버퍼길이를 얻기 위해 Out BUffer 파라미터를 NULL을 넣어 사이즈만 확인
	PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0);
	szCounters = new WCHAR[dwCounterSize];
	szInterfaces = new WCHAR[dwInterfaceSize];

	if (PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD,
		0) != ERROR_SUCCESS)
	{
		delete[] szCounters;
		delete[] szInterfaces;
		return;
	}


	iCnt = 0;
	szCur = szInterfaces;

	for (; *szCur != L'\0' && iCnt < df_PDH_ETHERNET_MAX; szCur += wcslen(szCur) + 1, iCnt++)
	{
		_EthernetStruct[iCnt]._bUse = true;
		_EthernetStruct[iCnt]._szName[0] = L'\0';
		wcscpy_s(_EthernetStruct[iCnt]._szName, szCur);
		szQuery[0] = L'\0';
		StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Received/sec", szCur);
		PdhAddCounter(_Query_PDH, szQuery, NULL, &_EthernetStruct[iCnt]._pdh_Counter_Network_RecvBytes);
		szQuery[0] = L'\0';
		StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Sent/sec", szCur);
		PdhAddCounter(_Query_PDH, szQuery, NULL, &_EthernetStruct[iCnt]._pdh_Counter_Network_SendBytes);
	}


	UpdateCPUTime();
	UpdateQuery();
}

//CPU 사용률 갱신, 500ms ~ 1s 단위 호출이 적절
void Monitoring_Tool::UpdateCPUTime()
{
	//포로세서 사용률 갱신
	//본래 사용 구조체 : FILETIME >> 100 ns 단위의 시간단위를 표현하는 구조체
	//	ㄴ ULARGE_INTEGER와 구조가 같으므로 이를 사용

	ULARGE_INTEGER Idle;
	ULARGE_INTEGER Kernel;
	ULARGE_INTEGER User;

	// 시스템 사용시간 획득
	// 아이들타임, 커널사용타임(아이들포함), 유저사용타임
	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false) return;

	ULONGLONG KernelDiff = Kernel.QuadPart - _ftProcessor_LastKernel.QuadPart;
	ULONGLONG UserDiff = User.QuadPart - _ftProcessor_LastUser.QuadPart;
	ULONGLONG IdleDiff = Idle.QuadPart - _ftProcessor_LastIdle.QuadPart;

	ULONGLONG Total = KernelDiff + UserDiff;
	ULONGLONG TimeDiff;

	if (Total == 0)
	{
		_fProcessorUser = 0.0f;
		_fProcessorKernel = 0.0f;
		_fProcessorTotal = 0.0f;
	}
	else
	{
		//커널에 아이들이 포함되어있으므로 빼서 계산
		_fProcessorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		_fProcessorUser = (float)((double)UserDiff / Total * 100.0f);
		_fProcessorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);
	}

	_ftProcessor_LastKernel = Kernel;
	_ftProcessor_LastUser = User;
	_ftProcessor_LastIdle = Idle;


	// 지정된 프로세스 사용률 갱신

	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;

	// 현재 100ns 단위 시간을 구함(UTC 시간) ... UTC 시간 : 세계 협정 시간, 한국은 UTC + 9

	// a = 샘플간격의 시스템 시간 (실제 경과시간)
	// b = 프로세스의 CPU 사용시간
	// a : 100 = b : 사용률


	//경과시간 측정
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime);

	//프로세스 사용시간 측정
	// 두,세번째 파라미터는 프로세스생성시간과 프로세스 종료시간으로 미사용할것, 종료되지않은경우 값이없음
	GetProcessTimes(_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);


	//이전에 저장한 프로세스 시간과의 차를 구해 실제로 얼마의 시간이 지났는지 확인
	// 그리고 실제 지나온 시간으로 나누면 사용률이 나옴
	TimeDiff = NowTime.QuadPart - _ftProcess_LastTime.QuadPart;
	UserDiff = User.QuadPart - _ftProcess_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - _ftProcess_LastKernel.QuadPart;

	Total = KernelDiff + UserDiff;

	_fProcessTotal = (float)(Total / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessKernel = (float)(KernelDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessUser = (float)(UserDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);

	_ftProcess_LastTime = NowTime;
	_ftProcess_LastKernel = Kernel;
	_ftProcess_LastUser = User;
}


void Monitoring_Tool::UpdateQuery()
{
	PdhCollectQueryData(_Query_PDH);

	PDH_FMT_COUNTERVALUE CounterVal;

	PdhGetFormattedCounterValue(_Counter_ProcessUserAllocMemory, PDH_FMT_LONG, NULL, &CounterVal);
	_lProcessUserAllocMemory = CounterVal.longValue;

	PdhGetFormattedCounterValue(_Counter_ProcessNonPagedMemory, PDH_FMT_LONG, NULL, &CounterVal);
	_lProcessNonPagedMemory = CounterVal.longValue;

	PdhGetFormattedCounterValue(_Counter_AvailableMemory, PDH_FMT_LONG, NULL, &CounterVal);
	_lAvailableMemory = CounterVal.longValue;

	PdhGetFormattedCounterValue(_Counter_NonPagedMemory, PDH_FMT_LONG, NULL, &CounterVal);
	_lNonPagedMemory = CounterVal.longValue;

	/////////////////////////////////////////////
	for (int iCnt = 0; iCnt < df_PDH_ETHERNET_MAX; iCnt++)
	{
		if (_EthernetStruct[iCnt]._bUse)
		{
			if (PdhGetFormattedCounterValue(_EthernetStruct[iCnt]._pdh_Counter_Network_RecvBytes,
				PDH_FMT_DOUBLE, NULL, &CounterVal) == ERROR_SUCCESS) _pdh_value_Network_RecvBytes += CounterVal.doubleValue;
			if (PdhGetFormattedCounterValue(_EthernetStruct[iCnt]._pdh_Counter_Network_SendBytes,
				PDH_FMT_DOUBLE, NULL, &CounterVal) == ERROR_SUCCESS) _pdh_value_Network_SendBytes += CounterVal.doubleValue;
		}
	}
}