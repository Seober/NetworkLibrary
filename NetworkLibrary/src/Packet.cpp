#include "Packet.h"

#include <windows.h>


Packet::Packet(int bufferSize) {
    RefCnt = 0;
    BufferSize = bufferSize;
    Front = 0;
    Rear = 0;


    Buffer.reset(new char[BufferSize]);

    EncodeFlag = false;
}

Packet::~Packet(void) {}

void Packet::Release(void) {}