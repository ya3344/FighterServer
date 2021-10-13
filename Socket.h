#pragma once
#include "Protocol.h"

class PacketBuffer;

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

	struct ClientInfo
	{
		SessionInfo* sessionInfo = nullptr;
		DWORD action = 0;
		BYTE direction = ACTION_MOVE_LL;
		BYTE moveDirection = 0;
		bool isMove = false;
		short x = 0;
		short y = 0;
		char hp = 0;
	};

public:
	bool Initialize();
	bool ServerProcess();
	void Update();
	void Release();

private:
	void SelectSocket(fd_set& read_set, fd_set& write_set, const vector<DWORD>& sessionID_Data);
	void SendProcess(const SessionInfo* sessionInfo);
	void RecvProcess(SessionInfo* sessionInfo);
	void PacketProcess(const WORD msgType, const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);
	void SendUnicast(const SessionInfo* sessionInfo, const HeaderInfo* header, const PacketBuffer& packetBuffer);
	void SendBroadcast(const SessionInfo* sessionInfo, const HeaderInfo* header, const PacketBuffer& packetBuffer, const bool isSelf = false);
private:
	void MoveStartRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);
	void MoveStartMakePacket(const ClientInfo* clientInfo, PacketBuffer& packetBuffer);
	void MoveStopRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);
	void MoveStopMakePacket(const ClientInfo* clientInfo, PacketBuffer& packetBuffer);
	void CreateCharacter_MakePacket(const ClientInfo* clientInfo);
	void CreateCharacterOther_MakePacket(const SessionInfo* sessionInfo);
	void RemoveCharacter_MakePacket(const ClientInfo* clientInfo);

private: // 캐릭터 업데이트 처리
	void CharacterUpdate();
	void ActionProc(ClientInfo* clientInfo);
	bool MoveCurX(short* outCurX, const bool isLeft);
	bool MoveCurY(short* outCurY, const bool isUp);

private:
	void AddSessionInfo(const SOCKET clientSock, SOCKADDR_IN& clientAddr);
	void RemoveSessionInfo(const DWORD sessionID);
	void HeaderNameInsert(); // Header Name debug 용
	void AddClientInfo(SessionInfo* sessionInfo);
	ClientInfo* FindClientInfo(const DWORD sessionID);

private:
	SOCKET mListenSock = INVALID_SOCKET;
	unordered_map<DWORD, SessionInfo*> mSessionData;
	unordered_map<DWORD, ClientInfo*> mClientData;

private:
	DWORD mSession_IDNum = 0;

private: // 패킷 헤더타입 디버그용
	unordered_map<WORD, wstring> mHeaderNameData;

private: // 프레임 고정 변수
	DWORD mOldTime = timeGetTime();
	DWORD mCurTime = timeGetTime();
	DWORD mFpsTime = timeGetTime();
	bool mIsFlag = false;
	DWORD mSkiptime = 0;
	DWORD mDeltaTime = mCurTime - mOldTime;
	int mFps = 0;
};

