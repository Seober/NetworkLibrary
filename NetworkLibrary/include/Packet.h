#pragma once
#include <windows.h>

class Packet {
public:
    static constexpr int kBufferDefault = 1024;
    static constexpr int kHeaderDefault = 20;

    Packet(int bufferSize = kBufferDefault);
    ~Packet(void);

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

    inline Packet& operator<<(unsigned char value);
    inline Packet& operator<<(char value);

    inline Packet& operator<<(short value);
    inline Packet& operator<<(unsigned short value);

    inline Packet& operator<<(int value);
    inline Packet& operator<<(long value);
    inline Packet& operator<<(unsigned long value);
    inline Packet& operator<<(float value);

    inline Packet& operator<<(__int64 value);
    inline Packet& operator<<(double value);

    inline Packet& operator<<(char* str);
    inline Packet& operator<<(WCHAR* str);


    inline Packet& operator>>(BYTE& value);
    inline Packet& operator>>(char& value);

    inline Packet& operator>>(short& value);
    inline Packet& operator>>(WORD& value);

    inline Packet& operator>>(int& value);
    inline Packet& operator>>(DWORD& value);
    inline Packet& operator>>(float& value);

    inline Packet& operator>>(__int64& value);
    inline Packet& operator>>(double& value);

    inline Packet& operator>>(char* str);
    inline Packet& operator>>(WCHAR* str);

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


void Packet::Clear(void) {
    Front = kHeaderDefault;
    Rear = kHeaderDefault;

    EncodeFlag = false;
}

int Packet::MoveWritePos(int size) {
    if (Rear + size > BufferSize)
        size = BufferSize - Rear;
    Rear += size;
    return size;
}

int Packet::MoveReadPos(int size) {
    if (Front + size > Rear)
        size = Rear - Front;
    Front += size;
    return size;
}

////////////////////////////////////////////////////////////////////////////////

int Packet::GetData(char* dest, int size) {
    if (Front + size > Rear)
        size = Rear - Front;

    memcpy(dest, &Buffer[Front], size);

    Front += size;

    return size;
}

int Packet::GetData(WCHAR* dest, int size) {
    size *= 2;
    if (Front + size > Rear)
        size = Rear - Front;

    memcpy(dest, &Buffer[Front], size);

    Front += size;

    return size;
}

int Packet::PutData(char* src, int srcSize) {
    if (Rear + srcSize > BufferSize)
        srcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], src, srcSize);

    Rear += srcSize;

    return srcSize;
}


int Packet::PutData(WCHAR* src, int srcSize) {
    srcSize *= 2;
    if (Rear + srcSize > BufferSize)
        srcSize = BufferSize - Rear;

    memcpy(&Buffer[Rear], src, srcSize);

    Rear += srcSize;

    return srcSize;
}

////////////////////////////////////////////////////////////////////////////////

Packet& Packet::operator<<(unsigned char value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned char));
    Rear += sizeof(unsigned char);

    return *this;
}

Packet& Packet::operator<<(char value) {
    memcpy(&Buffer[Rear], &value, sizeof(char));
    Rear += sizeof(char);

    return *this;
}

Packet& Packet::operator<<(short value) {
    memcpy(&Buffer[Rear], &value, sizeof(short));
    Rear += sizeof(value);

    return *this;
}

Packet& Packet::operator<<(unsigned short value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned short));
    Rear += sizeof(value);

    return *this;
}

Packet& Packet::operator<<(int value) {
    memcpy(&Buffer[Rear], &value, sizeof(int));
    Rear += sizeof(int);

    return *this;
}

Packet& Packet::operator<<(long value) {
    memcpy(&Buffer[Rear], &value, sizeof(long));
    Rear += sizeof(long);

    return *this;
}

Packet& Packet::operator<<(unsigned long value) {
    memcpy(&Buffer[Rear], &value, sizeof(unsigned long));
    Rear += sizeof(unsigned long);

    return *this;
}

Packet& Packet::operator<<(float value) {
    memcpy(&Buffer[Rear], &value, sizeof(float));
    Rear += sizeof(float);

    return *this;
}

Packet& Packet::operator<<(__int64 value) {
    memcpy(&Buffer[Rear], &value, sizeof(__int64));
    Rear += sizeof(__int64);

    return *this;
}

Packet& Packet::operator<<(double value) {
    memcpy(&Buffer[Rear], &value, sizeof(double));
    Rear += sizeof(double);

    return *this;
}

Packet& Packet::operator<<(char* str) {
    short len = (short)strlen(str);
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], str, len);
    Rear += sizeof(short) + len;

    return *this;
}


Packet& Packet::operator<<(WCHAR* str) {
    short len = (short)wcslen(str) * 2;
    memcpy(&Buffer[Rear], &len, sizeof(short));
    memcpy(&Buffer[Rear + sizeof(short)], str, len);
    Rear += sizeof(short) + len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////

Packet& Packet::operator>>(BYTE& value) {
    memcpy(&value, &Buffer[Front], sizeof(BYTE));
    Front += sizeof(BYTE);

    return *this;
}

Packet& Packet::operator>>(char& value) {
    memcpy(&value, &Buffer[Front], sizeof(char));
    Front += sizeof(char);

    return *this;
}

Packet& Packet::operator>>(short& value) {
    memcpy(&value, &Buffer[Front], sizeof(short));
    Front += sizeof(short);

    return *this;
}

Packet& Packet::operator>>(WORD& value) {
    memcpy(&value, &Buffer[Front], sizeof(WORD));
    Front += sizeof(WORD);

    return *this;
}

Packet& Packet::operator>>(int& value) {
    memcpy(&value, &Buffer[Front], sizeof(int));
    Front += sizeof(int);

    return *this;
}

Packet& Packet::operator>>(DWORD& value) {
    memcpy(&value, &Buffer[Front], sizeof(DWORD));
    Front += sizeof(DWORD);

    return *this;
}

Packet& Packet::operator>>(float& value) {
    memcpy(&value, &Buffer[Front], sizeof(float));
    Front += sizeof(float);

    return *this;
}

Packet& Packet::operator>>(__int64& value) {
    memcpy(&value, &Buffer[Front], sizeof(__int64));
    Front += sizeof(__int64);

    return *this;
}

Packet& Packet::operator>>(double& value) {
    memcpy(&value, &Buffer[Front], sizeof(double));
    Front += sizeof(double);

    return *this;
}

Packet& Packet::operator>>(char* str) {
    short* len = (short*)&Buffer[Front];
    memcpy(str, &Buffer[Front + sizeof(short)], *len);
    str[*len] = '\0';
    Front += sizeof(short) + *len;

    return *this;
}

Packet& Packet::operator>>(WCHAR* str) {
    short* len = (short*)&Buffer[Front];
    memcpy(str, &Buffer[Front + sizeof(short)], *len);
    str[*len] = L'\0';
    Front += sizeof(short) + *len;

    return *this;
}

////////////////////////////////////////////////////////////////////////////////