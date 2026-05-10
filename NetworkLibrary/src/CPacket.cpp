#include "CPacket.h"

#include <windows.h>


CPacket::CPacket(int bufferSize) {
    RefCnt = 0;
    BufferSize = bufferSize;
    Front = 0;
    Rear = 0;


    Buffer = new char[BufferSize];

    InitializeSRWLock(&srw_Encode);
    EncodeFlag = false;
}

CPacket::~CPacket(void) {
    delete Buffer;
}

void CPacket::Release(void) {}