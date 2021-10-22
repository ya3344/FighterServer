#pragma once
template <typename T>
inline void SafeDelete(T& obj)
{
	if (obj)
	{
		delete obj;
		obj = nullptr;
	}
}

template <typename T>
inline void SafeFree(T& obj)
{
	if (obj)
	{
		free(obj);
		obj = nullptr;
	}
}

inline short CalDistance(const short srcX, const short srcY, const short destX, const short destY)
{
	short x = destX - srcX;
	short y = destY - srcY;
	short distance = (short)(sqrt((x * x) + (y * y)));

	return distance;
}
	
// Log 

//inline void ConsoleLog(const WCHAR* buffer, const int logLevel)
//{
//	wprintf(L"[function:%s] %s\n", TEXT(__FUNCTION__),  buffer);
//	wsprintf(gLogBuffer, fmt, ##__VA_ARGS__);	\
//}

#define CONSOLE_LOG(logLevel, fmt, ...)							\
do {															\
	if (gLogLevel <= logLevel)									\
	{															\
		wsprintf(gLogBuffer, fmt, ##__VA_ARGS__);				\
		wprintf(L"[%s] %s\n", TEXT(__FUNCTION__), gLogBuffer);	\
	}															\
} while(0)														\

//#define CONSOLE_LOG(text) wprintf(L"function:%S %S\n", __FUNCTION__, text)
//#define CONSOLE_ERROR_LOG(text) wprintf(L"[ERROR] function:%S %S\n", __FUNCTION__, text)