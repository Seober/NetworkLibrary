#include "CPacket.h"

#include <windows.h>


CPacket::CPacket(int BufSize)
{
	m_iRefCnt = 0;
	m_iBufferSize = BufSize;
	m_iFront = 0;
	m_iRear = 0;


	cBuffer = new char[m_iBufferSize];

	InitializeSRWLock(&srw_Encode);
	EncodeFlag = false;
}

CPacket::~CPacket(void)
{
	delete cBuffer;
}

void CPacket::Release(void)
{

}