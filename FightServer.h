#pragma once
#include "Protocol.h"

class PacketBuffer;

class FightServer
{
public:
	FightServer() = default;
	~FightServer();

public:
	enum INFO_INDEX
	{
		SERVER_PORT = 20000,
		NAME_BUFFER_SIZE = 15,
		IP_BUFFER_SIZE = 16,
		MAX_PACKET_SIZE = 20,
		SECTOR_DIRECTION_POS = 9,
	};

	// Sector 관련
	struct SectorPosInfo
	{
		int x = 0;
		int y = 0;
	};
	struct SectorAroundInfo
	{
		int count = 0;
		SectorPosInfo arround[SECTOR_DIRECTION_POS];
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
		SectorPosInfo curSectorPos = { 0, };
		SectorPosInfo oldSectorPos = { 0, };
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
	void SendSector_Broadcast(const DWORD sessionID, const int sectorX, const int sectorY, const HeaderInfo& header, const PacketBuffer& packetBuffer, const bool isSelf = false);
	void SendAroundSector_Broadcast(const DWORD sessionID, const int sectorX, const int sectorY, const HeaderInfo& header, const PacketBuffer& packetBuffer, const bool isSelf = false);

private:
	void MoveStartRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);
	void MoveStartMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo);
	void MoveStopRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);
	void MoveStopMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo);
	void AttackRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer, const BYTE attackNum);
	void AttackMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo, const BYTE attackNum);
	void AttackCollision(const ClientInfo* clientInfo, const BYTE attackNum);
	void DamageMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const DWORD attackID, const DWORD victimID, const BYTE victimHP);
	void CreateCharacter_MakePacket(const ClientInfo* clientInfo);
	//void CreateCharacterOther_MakePacket(const SessionInfo* sessionInfo);
	void CreateOtherCharacter_MakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo);
	void RemoveCharacter_MakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo);
	void SyncMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo);
	void EchoRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer);


private: // 캐릭터 업데이트 처리
	void CharacterUpdate();
	void ActionProc(ClientInfo* clientInfo);
	bool MoveCurX(short* outCurX, const bool isLeft);
	bool MoveCurY(short* outCurY, const bool isUp);
	
private:
	void AddSessionInfo(const SOCKET clientSock, SOCKADDR_IN& clientAddr);
	unordered_map<DWORD, ClientInfo*>::iterator RemoveSessionInfo(const DWORD sessionID);
	void HeaderNameInsert(); // Header Name debug 용
	void AddClientInfo(SessionInfo* sessionInfo);
	ClientInfo* FindClientInfo(const DWORD sessionID);

//Sector 관련 함수
private:
	void SectorUpdateCharcater(ClientInfo* clientInfo, const bool isCreateCharacter = false);
	void GetSectorAround(int sectorX, int sectorY, SectorAroundInfo* sectorAround);
	void GetUpdateSectorAround(ClientInfo* clinetInfo, SectorAroundInfo* outRemoveSector, SectorAroundInfo* outAddSector);
	void SectorUpdatePacket(ClientInfo* clientInfo);
	void CreateSectorMyAroundPacket(ClientInfo* clientInfo);
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

private: // Section 관련 변수
	list<ClientInfo*> mSectorData[SECTOR_MAX_Y][SECTOR_MAX_X];
	POINT mSectorMaxRange;
};

