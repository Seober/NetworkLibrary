#pragma once
#include "CrashDump.h"

#define df_LOG_BUFSIZE_FILENAME 64
#define df_LOG_BUFSIZE_LOGTAG 128
#define df_LOG_BUFSIZE_LOGMESSAGE 256

class Logger
{
public:
	enum class en_LOG_LEVEL
	{
		eLEVEL_DEBUG,
		eLEVEL_ERROR,
		eLEVEL_SYSTEM
	};

	void Log(const WCHAR* szType, en_LOG_LEVEL LogLevel, const WCHAR* szStringFormat, ...);

	static Logger* GetInstance(void)
	{
		if (pLogger == NULL)
		{
			Lock();
			if (pLogger == NULL)
			{
				pLogger = new Logger;
				atexit(Destroy);
			}
			Unlock();
		}
		return pLogger;
	}

	void Crash(void) { Crasher.Crash(); }

	void SetLogLevel(en_LOG_LEVEL LogLevel) { Log_Level = LogLevel; }



private:
	Logger();

	static void Lock(void) { while (InterlockedExchange(&Key_Singleton, 1) != 0); }
	static void Unlock(void) { Key_Singleton = 0; }

	static void Destroy(void)
	{
		delete pLogger;
		pLogger = NULL;
	}


private:
	CrashDump Crasher;

	en_LOG_LEVEL Log_Level;

	unsigned __int64 _LogCnt;


	static long Key_Singleton;
	static Logger* pLogger;
};