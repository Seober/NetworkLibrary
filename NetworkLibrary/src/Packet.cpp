#include "Packet.h"

#include <windows.h>


Packet::Packet(int bufferSize) {
    RefCnt = 0;
    BufferSize = bufferSize;
    Front = 0;
    Rear = 0;


    Buffer = new char[BufferSize];

    EncodeFlag = false;
}

Packet::~Packet(void) {
    delete Buffer;
}

void Packet::Release(void) {}