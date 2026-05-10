#pragma once
#include <windows.h>

class CPacket {
public:
    static constexpr int kBufferDefault = 1024;
    static constexpr int kHeaderDefault = 20;

    CPacket(int bufferSize = kBufferDefault);
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
    inline int MoveWritePos(int size);
    inline int MoveReadPos(int size);

    inline CPacket& operator<<(unsigned char value);
    inline CPacket& operator<<(char value);

    inline CPacket& operator<<(short value);
    inline CPacket& operator<<(unsigned short value);

    inline CPacket& operator<<(int value);
    inline CPacket& operator<<(long value);
    inline CPacket& operator<<(unsigned long value);
    inline CPacket& operator<<(float value);

    inline CPacket& operator<<(__int64 value);
    inline CPacket& operator<<(double value);

    inline CPacket& operator<<(char* str);
    inline CPacket& operator<<(WCHAR* str);


    inline CPacket& operator>>(BYTE& value);
    inline CPacket& operator>>(char& value);

    inline CPacket& operator>>(short& value);
    inline CPacket& operator>>(WORD& value);

    inline CPacket& operator>>(int& value);
    inline CPacket& operator>>(DWORD& value);
    inline CPacket& operator>>(float& value);

    inline CPacket& operator>>(__int64& value);
    inline CPacket& operator>>(double& value);

    inline CPacket& operator>>(char* str);
    inline CPacket& operator>>(WCHAR* str);

    inline int GetData(char* dest, int size);
    inline int GetData(WCHAR* dest, int size);

    inline int PutData(char* src, int srcSize);
    inline int PutData(WCHAR* src, int srcSize);

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

int CPacket::MoveWritePos(int size) {
    if (Rear + size > BufferSize)
        size = BufferSize - Rear;
    Rear += size;
    return size;
}

int CPacket::MoveReadPos(int size) {
    if (Front + size > Rear)
        size = Rear - Front;
    Front += size;
    return size;
}

////////////////////////////////////////////////////////////////////////////////

int CPacket::GetData(char* dest, int size) {
    if (Front + size > Rear)
        size = Rear - Front;

    memcpy(dest, &Buffer[Front], size);

    Front += size;

    return size;
}

int CPacket::GetData(WCHAR* dest, int size) {
    size *= 2;
    if (Front + size > Rear)
        size = Rear - Front;

    memcpy(dest, &Buffer[Front], size);

    Front += size;

    return size;
}

int CPacket::PutData(char* src, int srcSize) {
    if (Rear + srcSize > BufferSize)
        srcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], src, srcSize);

    Rear += srcSize;

    return srcSize;
}


int CPacket::PutData(WCHAR* src, int srcSize) {
    srcSize *= 2;
    if (Rear + srcSize > BufferSize)
        srcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], src, srcSize);

    Rear += srcSize;

    return srcSize;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator<<(unsigned char value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned char));
    Rear += sizeof(unsigned char);

    return *this;
}

CPacket& CPacket::operator<<(char value) {
    memcpy(&Buffer[Rear], &value, sizeof(char));
    Rear += sizeof(char);

    return *this;
}

CPacket& CPacket::operator<<(short value) {
    memcpy(&Buffer[Rear], &value, sizeof(short));
    Rear += sizeof(value);

    return *this;
}

CPacket& CPacket::operator<<(unsigned short value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned short));
    Rear += sizeof(value);

    return *this;
}

CPacket& CPacket::operator<<(int value) {
    memcpy(&Buffer[Rear], &value, sizeof(int));
    Rear += sizeof(int);

    return *this;
}

CPacket& CPacket::operator<<(long value) {
    memcpy(&Buffer[Rear], &value, sizeof(long));
    Rear += sizeof(long);

    return *this;
}

CPacket& CPacket::operator<<(unsigned long value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned long));
    Rear += sizeof(unsigned long);

    return *this;
}

CPacket& CPacket::operator<<(float value) {
    memcpy(&Buffer[Rear], &value, sizeof(float));
    Rear += sizeof(float);

    return *this;
}

CPacket& CPacket::operator<<(__int64 value) {
    memcpy(&Buffer[Rear], &value, sizeof(__int64));
    Rear += sizeof(__int64);

    return *this;
}

CPacket& CPacket::operator<<(double value) {
    memcpy(&Buffer[Rear], &value, sizeof(double));
    Rear += sizeof(double);

    return *this;
}

CPacket& CPacket::operator<<(char* str) {
    short len = (short)strlen(str);
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], str, len);
    Rear += sizeof(short) + len;

    return *this;
}


CPacket& CPacket::operator<<(WCHAR* str) {
    short len = (short)wcslen(str) * 2;
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], str, len);
    Rear += sizeof(short) + len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////

CPacket& CPacket::operator>>(BYTE& value) {
    memcpy(&value, &Buffer[Front], sizeof(BYTE));
    Front += sizeof(BYTE);

    return *this;
}

CPacket& CPacket::operator>>(char& value) {
    memcpy(&value, &Buffer[Front], sizeof(char));
    Front += sizeof(char);

    return *this;
}

CPacket& CPacket::operator>>(short& value) {
    memcpy(&value, &Buffer[Front], sizeof(short));
    Front += sizeof(short);

    return *this;
}

CPacket& CPacket::operator>>(WORD& value) {
    memcpy(&value, &Buffer[Front], sizeof(WORD));
    Front += sizeof(WORD);

    return *this;
}

CPacket& CPacket::operator>>(int& value) {
    memcpy(&value, &Buffer[Front], sizeof(int));
    Front += sizeof(int);

    return *this;
}

CPacket& CPacket::operator>>(DWORD& value) {
    memcpy(&value, &Buffer[Front], sizeof(DWORD));
    Front += sizeof(DWORD);

    return *this;
}

CPacket& CPacket::operator>>(float& value) {
    memcpy(&value, &Buffer[Front], sizeof(float));
    Front += sizeof(float);

    return *this;
}

CPacket& CPacket::operator>>(__int64& value) {
    memcpy(&value, &Buffer[Front], sizeof(__int64));
    Front += sizeof(__int64);

    return *this;
}

CPacket& CPacket::operator>>(double& value) {
    memcpy(&value, &Buffer[Front], sizeof(double));
    Front += sizeof(double);

    return *this;
}

CPacket& CPacket::operator>>(char* str) {
    short* len = (short*)&Buffer[Front];
    memcpy(str, &Buffer[Front + sizeof(short)], *len);
    str[*len] = '\0';
    Front += sizeof(short) + *len;

    return *this;
}

CPacket& CPacket::operator>>(WCHAR* str) {
    short* len = (short*)&Buffer[Front];
    memcpy(str, &Buffer[Front + sizeof(short)], *len);
    str[*len] = L'\0';
    Front += sizeof(short) + *len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////