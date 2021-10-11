#pragma once
#include "Protocol.h"
class Socket
{
public:
	Socket() = default;
	~Socket();

public:
	enum INFO_INDEX
	{
		SERVER_PORT = 20000,
		NAME_BUFFER_SIZE = 15,
		IP_BUFFER_SIZE = 16,
		MAX_PACKET_SIZE = 20,
	};

	struct SessionInfo
	{
		SOCKET clientSock = INVALID_SOCKET;
		WCHAR ip[IP_BUFFER_SIZE] = { 0, };
		WORD port = 0;
		DWORD sessionID = 0;
		class RingBuffer* sendRingBuffer = nullptr;
		class RingBuffer* recvRingBuffer = nullptr;
		DWORD recvTime = 0;
	};

public:
	bool Initialize();
	bool ServerProcess();

private:
	void SelectSocket(fd_set& read_set, fd_set& write_set, const vector<DWORD>& sessionID_Data);
	void SendProcess(const SessionInfo* sessionInfo);
	void RecvProcess(SessionInfo* sessionInfo);
	void PacketProcess(const WORD msgType, SessionInfo* clientInfo);

private:
	void AddSessionInfo(const SOCKET clientSock, SOCKADDR_IN& clientAddr);
	void RemoveSessionInfo(SessionInfo* sessionInfo);
	void HeaderNameInsert(); // Header Name debug 용
	

private:
	SOCKET mListenSock = INVALID_SOCKET;
	unordered_map<DWORD, SessionInfo*> mSessionData;
	class PacketBuffer* mPacketBuffer = nullptr;

private:
	DWORD mSession_IDNum = 0;

private: // 패킷 헤더타입 디버그용
	unordered_map<WORD, wstring> mHeaderNameData;
};

