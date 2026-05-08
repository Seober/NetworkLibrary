#pragma once
#include <windows.h>

class CPacket {
public:
    static constexpr int kBufferDefault = 1024;
    static constexpr int kHeaderDefault = 20;

    CPacket(int BufferSize = kBufferDefault);
    ~CPacket(void);

    void Release(void);

    inline void Clear(void);

    int GetBufferSize(void) { return BufferSize; }
    int GetDataSize(void) { return Rear - Front; }
    int GetFreeSize(void) { return GetBufferSize() - GetDataSize(); }

    char* GetReadBufferPtr(void) { return &Buffer[Front]; }
    char* GetWriteBufferPtr(void) { return &Buffer[Rear]; }

    char* GetFrontPtr(void) { return Buffer; }
    void Front_MoveTo(int idx) { Front = idx; }
    void Rear_MoveTo(int idx) { Rear = idx; }

    bool CheckFlag_Encode(void) { return EncodeFlag; }
    void LockPacket(void) { AcquireSRWLockExclusive(&srw_Encode); }
    void UnlockPacket(void) {
        EncodeFlag = true;
        ReleaseSRWLockExclusive(&srw_Encode);
    }


    // GetBufferPtr을 이용해서 강제로 버퍼내용을 수정할 경우
    inline int MoveWritePos(int iSize);
    inline int MoveReadPos(int iSize);

    inline CPacket& operator<<(unsigned char byValue);
    inline CPacket& operator<<(char chValue);

    inline CPacket& operator<<(short shValue);
    inline CPacket& operator<<(unsigned short wValue);

    inline CPacket& operator<<(int iValue);
    inline CPacket& operator<<(long lValue);
    inline CPacket& operator<<(unsigned long lValue);
    inline CPacket& operator<<(float fValue);

    inline CPacket& operator<<(__int64 iValue);
    inline CPacket& operator<<(double dValue);

    inline CPacket& operator<<(char* cString);
    inline CPacket& operator<<(WCHAR* wString);


    inline CPacket& operator>>(BYTE& byValue);
    inline CPacket& operator>>(char& chValue);

    inline CPacket& operator>>(short& shValue);
    inline CPacket& operator>>(WORD& wValue);

    inline CPacket& operator>>(int& iValue);
    inline CPacket& operator>>(DWORD& dwValue);
    inline CPacket& operator>>(float& fValue);

    inline CPacket& operator>>(__int64& iValue);
    inline CPacket& operator>>(double& dValue);

    inline CPacket& operator>>(char* cString);
    inline CPacket& operator>>(WCHAR* wString);

    inline int GetData(char* chpDest, int iSize);
    inline int GetData(WCHAR* wcpDest, int iSize);

    inline int PutData(char* shpSrc, int iSrcSize);
    inline int PutData(WCHAR* wcpSrc, int iSrcSize);

    inline void ShiftDataToFront(void) {
        int DataSize = GetDataSize();
        memcpy(Buffer, &Buffer[Front], DataSize);
        Front = 0;
        Rear = DataSize;
    }

    inline int IncrementRef(void) { return InterlockedIncrement(&RefCnt); }
    inline int DecrementRef(void) { return InterlockedDecrement(&RefCnt); }

    inline int GetRef(void) { return InterlockedExchange(&RefCnt, RefCnt); }


private:
    volatile unsigned int RefCnt;

private:
    int BufferSize;

private:
    int Front;
    int Rear;

private:
    char* Buffer;

private:
    SRWLOCK srw_Encode;
    bool EncodeFlag;
};


void CPacket::Clear(void) {
    Front = kHeaderDefault;
    Rear = kHeaderDefault;

    EncodeFlag = false;
}

int CPacket::MoveWritePos(int iSize) {
    if (Rear + iSize > BufferSize)
        iSize = BufferSize - Rear;
    Rear += iSize;
    return iSize;
}

int CPacket::MoveReadPos(int iSize) {
    if (Front + iSize > Rear)
        iSize = Rear - Front;
    Front += iSize;
    return iSize;
}

////////////////////////////////////////////////////////////////////////////////

int CPacket::GetData(char* chpDest, int iSize) {
    if (Front + iSize > Rear)
        iSize = Rear - Front;

    memcpy(chpDest, &Buffer[Front], iSize);

    Front += iSize;

    return iSize;
}

int CPacket::GetData(WCHAR* wcpDest, int iSize) {
    iSize *= 2;
    if (Front + iSize > Rear)
        iSize = Rear - Front;

    memcpy(wcpDest, &Buffer[Front], iSize);

    Front += iSize;

    return iSize;
}

int CPacket::PutData(char* shpSrc, int iSrcSize) {
    if (Rear + iSrcSize > BufferSize)
        iSrcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], shpSrc, iSrcSize);

    Rear += iSrcSize;

    return iSrcSize;
}


int CPacket::PutData(WCHAR* wcpSrc, int iSrcSize) {
    iSrcSize *= 2;
    if (Rear + iSrcSize > BufferSize)
        iSrcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], wcpSrc, iSrcSize);

    Rear += iSrcSize;

    return iSrcSize;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator<<(unsigned char byValue) {
    memcpy(&Buffer[Rear], &byValue, sizeof(unsigned char));
    Rear += sizeof(unsigned char);

    return *this;
}

CPacket& CPacket::operator<<(char chValue) {
    memcpy(&Buffer[Rear], &chValue, sizeof(char));
    Rear += sizeof(char);

    return *this;
}

CPacket& CPacket::operator<<(short shValue) {
    memcpy(&Buffer[Rear], &shValue, sizeof(short));
    Rear += sizeof(shValue);

    return *this;
}

CPacket& CPacket::operator<<(unsigned short wValue) {
    memcpy(&Buffer[Rear], &wValue, sizeof(unsigned short));
    Rear += sizeof(wValue);

    return *this;
}

CPacket& CPacket::operator<<(int iValue) {
    memcpy(&Buffer[Rear], &iValue, sizeof(int));
    Rear += sizeof(int);

    return *this;
}

CPacket& CPacket::operator<<(long lValue) {
    memcpy(&Buffer[Rear], &lValue, sizeof(long));
    Rear += sizeof(long);

    return *this;
}

CPacket& CPacket::operator<<(unsigned long lValue) {
    memcpy(&Buffer[Rear], &lValue, sizeof(unsigned long));
    Rear += sizeof(unsigned long);

    return *this;
}

CPacket& CPacket::operator<<(float fValue) {
    memcpy(&Buffer[Rear], &fValue, sizeof(float));
    Rear += sizeof(float);

    return *this;
}

CPacket& CPacket::operator<<(__int64 iValue) {
    memcpy(&Buffer[Rear], &iValue, sizeof(__int64));
    Rear += sizeof(__int64);

    return *this;
}

CPacket& CPacket::operator<<(double dValue) {
    memcpy(&Buffer[Rear], &dValue, sizeof(double));
    Rear += sizeof(double);

    return *this;
}

CPacket& CPacket::operator<<(char* cString) {
    short len = (short)strlen(cString);
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], cString, len);
    Rear += sizeof(short) + len;

    return *this;
}


CPacket& CPacket::operator<<(WCHAR* wString) {
    short len = (short)wcslen(wString) * 2;
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], wString, len);
    Rear += sizeof(short) + len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator>>(BYTE& byValue) {
    memcpy(&byValue, &Buffer[Front], sizeof(BYTE));
    Front += sizeof(BYTE);

    return *this;
}

CPacket& CPacket::operator>>(char& chValue) {
    memcpy(&chValue, &Buffer[Front], sizeof(char));
    Front += sizeof(char);

    return *this;
}

CPacket& CPacket::operator>>(short& shValue) {
    memcpy(&shValue, &Buffer[Front], sizeof(short));
    Front += sizeof(short);

    return *this;
}

CPacket& CPacket::operator>>(WORD& wValue) {
    memcpy(&wValue, &Buffer[Front], sizeof(WORD));
    Front += sizeof(WORD);

    return *this;
}

CPacket& CPacket::operator>>(int& iValue) {
    memcpy(&iValue, &Buffer[Front], sizeof(int));
    Front += sizeof(int);

    return *this;
}

CPacket& CPacket::operator>>(DWORD& dwValue) {
    memcpy(&dwValue, &Buffer[Front], sizeof(DWORD));
    Front += sizeof(DWORD);

    return *this;
}

CPacket& CPacket::operator>>(float& fValue) {
    memcpy(&fValue, &Buffer[Front], sizeof(float));
    Front += sizeof(float);

    return *this;
}

CPacket& CPacket::operator>>(__int64& iValue) {
    memcpy(&iValue, &Buffer[Front], sizeof(__int64));
    Front += sizeof(__int64);

    return *this;
}

CPacket& CPacket::operator>>(double& dValue) {
    memcpy(&dValue, &Buffer[Front], sizeof(double));
    Front += sizeof(double);

    return *this;
}

CPacket& CPacket::operator>>(char* cString) {
    short* len = (short*)&Buffer[Front];
    memcpy(cString, &Buffer[Front + sizeof(short)], *len);
    cString[*len] = '\0';
    Front += sizeof(short) + *len;

    return *this;
}

CPacket& CPacket::operator>>(WCHAR* wString) {
    short* len = (short*)&Buffer[Front];
    memcpy(wString, &Buffer[Front + sizeof(short)], *len);
    wString[*len] = L'\0';
    Front += sizeof(short) + *len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////