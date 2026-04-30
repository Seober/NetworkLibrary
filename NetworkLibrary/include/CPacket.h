#pragma once
#include <windows.h>

class CPacket
{
public:
	enum en_PACKET
	{
		eBUFFER_DEFAULT = 1024,
		eHEADER_DEFAULT = 20
	};

	CPacket(int BufferSize = eBUFFER_DEFAULT);
	~CPacket(void);

	void Release(void);

	inline void Clear(void);

	int GetBufferSize(void) { return m_iBufferSize; }
	int GetDataSize(void) { return m_iRear - m_iFront; }
	int GetFreeSize(void) { return GetBufferSize() - GetDataSize(); }

	char* GetReadBufferPtr(void) { return &cBuffer[m_iFront]; }
	char* GetWriteBufferPtr(void) { return &cBuffer[m_iRear]; }

	char* GetFrontPtr(void) { return cBuffer; }
	void Front_MoveTo(int idx) { m_iFront = idx; }
	void Rear_MoveTo(int idx) { m_iRear = idx; }

	bool CheckFlag_Encode(void) { return EncodeFlag; }
	void LockPacket(void) { AcquireSRWLockExclusive(&srw_Encode); }
	void UnlockPacket(void)
	{
		EncodeFlag = true;
		ReleaseSRWLockExclusive(&srw_Encode);
	}


	// GetBufferPtr을 이용해서 강제로 버퍼내용을 수정할 경우
	inline int MoveWritePos(int iSize);
	inline int MoveReadPos(int iSize);

	inline CPacket& operator << (unsigned char byValue);
	inline CPacket& operator << (char chValue);

	inline CPacket& operator << (short shValue);
	inline CPacket& operator << (unsigned short wValue);

	inline CPacket& operator << (int iValue);
	inline CPacket& operator << (long lValue);
	inline CPacket& operator << (unsigned long lValue);
	inline CPacket& operator << (float fValue);

	inline CPacket& operator << (__int64 iValue);
	inline CPacket& operator << (double dValue);

	inline CPacket& operator << (char* cString);
	inline CPacket& operator << (WCHAR* wString);


	inline CPacket& operator >> (BYTE& byValue);
	inline CPacket& operator >> (char& chValue);

	inline CPacket& operator >> (short& shValue);
	inline CPacket& operator >> (WORD& wValue);

	inline CPacket& operator >> (int& iValue);
	inline CPacket& operator >> (DWORD& dwValue);
	inline CPacket& operator >> (float& fValue);

	inline CPacket& operator >> (__int64& iValue);
	inline CPacket& operator >> (double& dValue);

	inline CPacket& operator >> (char* cString);
	inline CPacket& operator >> (WCHAR* wString);

	inline int GetData(char* chpDest, int iSize);
	inline int GetData(WCHAR* wcpDest, int iSize);

	inline int PutData(char* shpSrc, int iSrcSize);
	inline int PutData(WCHAR* wcpSrc, int iSrcSize);

	inline void ShiftDataToFront(void)
	{
		int DataSize = GetDataSize();
		memcpy(cBuffer, &cBuffer[m_iFront], DataSize);
		m_iFront = 0;
		m_iRear = DataSize;
	}

	inline int IncrementRef(void) { return InterlockedIncrement(&m_iRefCnt); }
	inline int DecrementRef(void) { return InterlockedDecrement(&m_iRefCnt); }

	inline int GetRef(void) { return InterlockedExchange(&m_iRefCnt, m_iRefCnt); }


private:
	volatile unsigned int m_iRefCnt;

private:
	int m_iBufferSize;

private:
	int m_iFront;
	int m_iRear;

private:
	char* cBuffer;

private:
	SRWLOCK srw_Encode;
	bool EncodeFlag;
};


void CPacket::Clear(void)
{
	m_iFront = eHEADER_DEFAULT;
	m_iRear = eHEADER_DEFAULT;

	EncodeFlag = false;
}

int CPacket::MoveWritePos(int iSize)
{
	if (m_iRear + iSize > m_iBufferSize) iSize = m_iBufferSize - m_iRear;
	m_iRear += iSize;
	return iSize;
}

int CPacket::MoveReadPos(int iSize)
{
	if (m_iFront + iSize > m_iRear) iSize = m_iRear - m_iFront;
	m_iFront += iSize;
	return iSize;
}

////////////////////////////////////////////////////////////////////////////////

int CPacket::GetData(char* chpDest, int iSize)
{
	if (m_iFront + iSize > m_iRear) iSize = m_iRear - m_iFront;

	memcpy(chpDest, &cBuffer[m_iFront], iSize);

	m_iFront += iSize;

	return iSize;

}

int CPacket::GetData(WCHAR* wcpDest, int iSize)
{
	iSize *= 2;
	if (m_iFront + iSize > m_iRear) iSize = m_iRear - m_iFront;

	memcpy(wcpDest, &cBuffer[m_iFront], iSize);

	m_iFront += iSize;

	return iSize;
}

int CPacket::PutData(char* shpSrc, int iSrcSize)
{
	if (m_iRear + iSrcSize > m_iBufferSize) iSrcSize = m_iBufferSize - m_iRear;

	memcpy(&cBuffer[m_iRear], shpSrc, iSrcSize);

	m_iRear += iSrcSize;

	return iSrcSize;
}


int CPacket::PutData(WCHAR* wcpSrc, int iSrcSize)
{
	iSrcSize *= 2;
	if (m_iRear + iSrcSize > m_iBufferSize) iSrcSize = m_iBufferSize - m_iRear;

	memcpy(&cBuffer[m_iRear], wcpSrc, iSrcSize);

	m_iRear += iSrcSize;

	return iSrcSize;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator << (unsigned char byValue)
{
	memcpy(&cBuffer[m_iRear], &byValue, sizeof(unsigned char));
	m_iRear += sizeof(unsigned char);

	return *this;
}

CPacket& CPacket::operator << (char chValue)
{
	memcpy(&cBuffer[m_iRear], &chValue, sizeof(char));
	m_iRear += sizeof(char);

	return *this;
}

CPacket& CPacket::operator << (short shValue)
{
	memcpy(&cBuffer[m_iRear], &shValue, sizeof(short));
	m_iRear += sizeof(shValue);

	return *this;
}

CPacket& CPacket::operator << (unsigned short wValue)
{
	memcpy(&cBuffer[m_iRear], &wValue, sizeof(unsigned short));
	m_iRear += sizeof(wValue);

	return *this;
}

CPacket& CPacket::operator << (int iValue)
{
	memcpy(&cBuffer[m_iRear], &iValue, sizeof(int));
	m_iRear += sizeof(int);

	return *this;
}

CPacket& CPacket::operator << (long lValue)
{
	memcpy(&cBuffer[m_iRear], &lValue, sizeof(long));
	m_iRear += sizeof(long);

	return *this;
}

CPacket& CPacket::operator << (unsigned long lValue)
{
	memcpy(&cBuffer[m_iRear], &lValue, sizeof(unsigned long));
	m_iRear += sizeof(unsigned long);

	return *this;
}

CPacket& CPacket::operator << (float fValue)
{
	memcpy(&cBuffer[m_iRear], &fValue, sizeof(float));
	m_iRear += sizeof(float);

	return *this;
}

CPacket& CPacket::operator << (__int64 iValue)
{
	memcpy(&cBuffer[m_iRear], &iValue, sizeof(__int64));
	m_iRear += sizeof(__int64);

	return *this;
}

CPacket& CPacket::operator << (double dValue)
{
	memcpy(&cBuffer[m_iRear], &dValue, sizeof(double));
	m_iRear += sizeof(double);

	return *this;
}

CPacket& CPacket::operator << (char* cString)
{
	short len = (short)strlen(cString);
	memcpy(&cBuffer[m_iRear], &len, sizeof(short));
	memcpy(&cBuffer[m_iRear + sizeof(short)], cString, len);
	m_iRear += sizeof(short) + len;

	return *this;
}


CPacket& CPacket::operator << (WCHAR* wString)
{
	short len = (short)wcslen(wString) * 2;
	memcpy(&cBuffer[m_iRear], &len, sizeof(short));
	memcpy(&cBuffer[m_iRear + sizeof(short)], wString, len);
	m_iRear += sizeof(short) + len;

	return *this;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator >> (BYTE& byValue)
{
	memcpy(&byValue, &cBuffer[m_iFront], sizeof(BYTE));
	m_iFront += sizeof(BYTE);

	return *this;
}

CPacket& CPacket::operator >> (char& chValue)
{
	memcpy(&chValue, &cBuffer[m_iFront], sizeof(char));
	m_iFront += sizeof(char);

	return *this;
}

CPacket& CPacket::operator >> (short& shValue)
{
	memcpy(&shValue, &cBuffer[m_iFront], sizeof(short));
	m_iFront += sizeof(short);

	return *this;
}

CPacket& CPacket::operator >> (WORD& wValue)
{
	memcpy(&wValue, &cBuffer[m_iFront], sizeof(WORD));
	m_iFront += sizeof(WORD);

	return *this;
}

CPacket& CPacket::operator >> (int& iValue)
{
	memcpy(&iValue, &cBuffer[m_iFront], sizeof(int));
	m_iFront += sizeof(int);

	return *this;
}

CPacket& CPacket::operator >> (DWORD& dwValue)
{
	memcpy(&dwValue, &cBuffer[m_iFront], sizeof(DWORD));
	m_iFront += sizeof(DWORD);

	return *this;
}

CPacket& CPacket::operator >> (float& fValue)
{
	memcpy(&fValue, &cBuffer[m_iFront], sizeof(float));
	m_iFront += sizeof(float);

	return *this;
}

CPacket& CPacket::operator >> (__int64& iValue)
{
	memcpy(&iValue, &cBuffer[m_iFront], sizeof(__int64));
	m_iFront += sizeof(__int64);

	return *this;
}

CPacket& CPacket::operator >> (double& dValue)
{
	memcpy(&dValue, &cBuffer[m_iFront], sizeof(double));
	m_iFront += sizeof(double);

	return *this;
}

CPacket& CPacket::operator >> (char* cString)
{
	short* len = (short*)&cBuffer[m_iFront];
	memcpy(cString, &cBuffer[m_iFront + sizeof(short)], *len);
	cString[*len] = '\0';
	m_iFront += sizeof(short) + *len;

	return *this;
}

CPacket& CPacket::operator >> (WCHAR* wString)
{
	short* len = (short*)&cBuffer[m_iFront];
	memcpy(wString, &cBuffer[m_iFront + sizeof(short)], *len);
	wString[*len] = L'\0';
	m_iFront += sizeof(short) + *len;

	return *this;
}

////////////////////////////////////////////////////////////////////////////////