#include "pch.h"
#include "Socket.h"
#include "../Common\RingBuffer/RingBuffer.h"
#include "../Common\PacketBuffer/PacketBuffer.h"

Socket::~Socket()
{
	closesocket(mListenSock);
	WSACleanup();
	mHeaderNameData.clear();
	Release();
}

bool Socket::Initialize()
{
	WSADATA wsaData;
	SOCKADDR_IN serveraddr;
	WCHAR serverIP[IP_BUFFER_SIZE] = { 0, };
	LINGER optval;
	int returnValue;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"WSAStartup() errcode[%d]", WSAGetLastError());
		return false;
	}

	mListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == mListenSock)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"listen_sock error:%d", WSAGetLastError());
		return false;
	}

	// timewait ������ �ʵ��� ����
	optval.l_onoff = 1;
	optval.l_linger = 0;
	setsockopt(mListenSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));

	// bind
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVER_PORT);
	InetNtop(AF_INET, &serveraddr.sin_addr, serverIP, _countof(serverIP));

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[CHAT SERVER] SERVER IP: %s SERVER Port:%d", serverIP, ntohs(serveraddr.sin_port));

	returnValue = bind(mListenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (returnValue == SOCKET_ERROR)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"bind error:%d", WSAGetLastError());
		return false;
	}

	//listen
	returnValue = listen(mListenSock, SOMAXCONN);
	if (returnValue == SOCKET_ERROR)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"listen error:%d ", WSAGetLastError());
		return false;
	}
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"server open\n");

	// ����ŷ �������� ��ȯ
	u_long on = 1;
	if (SOCKET_ERROR == ioctlsocket(mListenSock, FIONBIO, &on))
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"ioctlsocket() errcode[%d]", WSAGetLastError());
		return false;
	}

	// ����׿� ���Ÿ�� �̸� ����
	HeaderNameInsert();
	
	// ���� ���� ����
	mSectorRange.x = RANGE_MOVE_RIGHT / SECTOR_MAX_X;
	mSectorRange.y = RANGE_MOVE_BOTTOM / SECTOR_MAX_Y;

	return true;
}

bool Socket::ServerProcess()
{
	fd_set read_set;
	fd_set write_set;
	SessionInfo* sessionInfo = nullptr;
	BYTE socketCount = 0;
	vector<DWORD> sessionID_Data;

	// userid �� �ִ� fdsetsize ��ŭ ���� ������ �̸� �޸� �Ҵ� �Ͽ� reallocation ����
	sessionID_Data.reserve(FD_SETSIZE);

	while (true)
	{
		// ���� �� �ʱ�ȭ
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		// ���� ���� ��
		FD_SET(mListenSock, &read_set);

		// ���� ������ �ʱ�ȭ
		sessionID_Data.clear();
		socketCount = 0;

		// Ŭ���̾�Ʈ ���� ��
		for (auto sessionData : mSessionData)
		{
			sessionInfo = sessionData.second;
			FD_SET(sessionInfo->clientSock, &read_set);
			sessionID_Data.emplace_back(sessionInfo->sessionID);

			if (sessionInfo->sendRingBuffer->GetUseSize() > 0)
			{
				FD_SET(sessionInfo->clientSock, &write_set);
			}

			// 64���� ��� select ����
			if (socketCount >= FD_SETSIZE)
			{
				socketCount = 0;
				SelectSocket(read_set, write_set, sessionID_Data);

				// ���� �� �ʱ�ȭ
				FD_ZERO(&read_set);
				FD_ZERO(&write_set);
				// ���� ����Ʈ �ʱ�ȭ
				sessionID_Data.clear();
				// ���� ���� ��
				FD_SET(mListenSock, &read_set);
			}
			else
			{
				++socketCount;
			}
		}
		// 64�� �̸��� select ����
		SelectSocket(read_set, write_set, sessionID_Data);

		// Update ó��
		Update();
	}

	return false;
}

void Socket::Update()
{
	mCurTime = timeGetTime();

	if (mIsFlag)
	{
		mOldTime = mCurTime - (mDeltaTime - (20 - (mCurTime - mSkiptime)));
		mIsFlag = false;
	}

	mDeltaTime = mCurTime - mOldTime;

	if (mDeltaTime < 40)
	{
		if (mDeltaTime < 20)
		{
			Sleep(20 - mDeltaTime);
		}
		//20ms�� �ʰ� X -> Sleep �� �ð���ŭ old�� ���Ѵ�. 
		//20ms�� �ʰ� O -> �ʰ��� �ð���ŭ old���� ����. (�׳� �����ϴٰ� 1�������� ������ ��ŵ) old = cur - (deltaTime - 20);
		mOldTime = mCurTime - (mDeltaTime - 20);
		CharacterUpdate();
		++mFps;
	}
	else
	{
		mIsFlag = true;
		mSkiptime = mCurTime;
	}

	if (mFpsTime + 1000 < timeGetTime())
	{
		//wprintf(L"fps:%d\n", mFps);
		mFpsTime = timeGetTime();
		mFps = 0;
	}
}

void Socket::Release()
{
	ClientInfo* clientInfo = nullptr;
	SessionInfo* sessionInfo = nullptr;

	for (auto iterClientData : mClientData)
	{
		clientInfo = iterClientData.second;
		SafeDelete(clientInfo);
	}
	mClientData.clear();

	for (auto iterSessionData : mSessionData)
	{
		sessionInfo = iterSessionData.second;
		SafeDelete(sessionInfo->sendRingBuffer);
		SafeDelete(sessionInfo->recvRingBuffer);
		closesocket(sessionInfo->clientSock);
		SafeDelete(sessionInfo);
	}
	mSessionData.clear();

}

void Socket::SelectSocket(fd_set& read_set, fd_set& write_set, const vector<DWORD>& sessionID_Data)
{
	TIMEVAL timeout;
	int fdNum;
	int addrlen;
	int returnVal;
	SOCKET clientSock = INVALID_SOCKET;
	WCHAR clientIP[IP_BUFFER_SIZE] = { 0, };
	SOCKADDR_IN clientaddr;
	char inputData[RingBuffer::MAX_BUFFER_SIZE] = { 0, };
	SessionInfo* sessionInfo = nullptr;

	// select ��� ����
	timeout.tv_sec = 0;
	timeout.tv_sec = 0;

	fdNum = select(0, &read_set, &write_set, 0, &timeout);

	if (fdNum == 0)
		return;
	else if (fdNum == SOCKET_ERROR)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"SOCKET_ERROR() errcode[%d]", WSAGetLastError());
		return;
	}
	// ���� �� �˻�
	if (FD_ISSET(mListenSock, &read_set))
	{
		addrlen = sizeof(clientaddr);
		clientSock = accept(mListenSock, (SOCKADDR*)&clientaddr, &addrlen);
		if (clientSock == INVALID_SOCKET)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"accept error:%d", WSAGetLastError());
			return;
		}
		InetNtop(AF_INET, &clientaddr.sin_addr, clientIP, 16);
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[CHAT SERVER] Client IP: %s Clinet Port:%d", clientIP, ntohs(clientaddr.sin_port));

		// ���� ���� �߰�
		AddSessionInfo(clientSock, clientaddr);
	}

	for (const DWORD sessionID : sessionID_Data)
	{
		auto sessionData = mSessionData.find(sessionID);
		if (sessionData == mSessionData.end())
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"sessionId find error[sessionID:%d]", sessionID);
			return;
		}

		sessionInfo = sessionData->second;
		_ASSERT(sessionInfo != nullptr);
		if (FD_ISSET(sessionInfo->clientSock, &read_set))
		{
			if (returnVal = recv(sessionInfo->clientSock, sessionInfo->recvRingBuffer->GetBufferPtr(), sessionInfo->recvRingBuffer->GetFreeSize(), 0))
			{
				CONSOLE_LOG(LOG_LEVEL_DEBUG, L"sessionID:%d recv packet size:%d", sessionInfo->sessionID, returnVal);

				if (returnVal == SOCKET_ERROR)
				{
					// ���� ���� ���� ����
					if (WSAGetLastError() == WSAECONNRESET)
					{
						RemoveSessionInfo(sessionInfo->sessionID);
						continue;
					}
					// �߸��� �ּ�
					if (WSAGetLastError() == WSAEFAULT)
					{
						RemoveSessionInfo(sessionInfo->sessionID);
						continue;
					}
					if (WSAGetLastError() != WSAEWOULDBLOCK)
					{
						CONSOLE_LOG(LOG_LEVEL_ERROR, L"recv ERROR errcode[%d]", WSAGetLastError());
						continue;
					}

				}

				returnVal = sessionInfo->recvRingBuffer->MoveWritePos(returnVal);
				if (returnVal == RingBuffer::USE_COUNT_OVER_FLOW)
				{
					CONSOLE_LOG(LOG_LEVEL_ERROR, L"recv enqueue USE_COUNT_OVER_FLOW returnVal[%d]", returnVal);
					continue;
				}
				RecvProcess(sessionInfo);
			}
			else
			{
				RemoveSessionInfo(sessionInfo->sessionID);
			}
		}
		if (FD_ISSET(sessionInfo->clientSock, &write_set))
		{
			SendProcess(sessionInfo);
		}
	}
}

void Socket::SendProcess(const SessionInfo* sessionInfo)
{
	int retSize = 0;
	int retVal = 0;
	char outputData[RingBuffer::MAX_BUFFER_SIZE] = { 0, };

	if (sessionInfo->clientSock == INVALID_SOCKET)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"clientSock INVALID_SOCKET error");
		return;
	}

	if (sessionInfo->sendRingBuffer->GetUseSize() <= 0)
		return;

	// �������� �ִ����� ����
	retSize = sessionInfo->sendRingBuffer->Peek(outputData, sessionInfo->sendRingBuffer->GetUseSize());

	retVal = send(sessionInfo->clientSock, outputData, retSize, 0);
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"sessionID:%d send packet size:%d sendRingBuffer useSize:%d ", 
		sessionInfo->sessionID, retVal, sessionInfo->sendRingBuffer->GetUseSize() );

	if (retVal == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Send() WSAGetLastError [errcode:%d][sessionID:%d]", 
				WSAGetLastError(), sessionInfo->sessionID);

			return;
		}
		// WSAEWOULDBLOCK �����̱� ������ ������ �ƴϹǷ� �ٷ� ����
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"WSAEWOULDBLOCK [sessionID:%d]", sessionInfo->sessionID);
		return;
	}
	retVal = sessionInfo->sendRingBuffer->MoveReadPos(retVal);

	if (retVal == RingBuffer::USE_COUNT_UNDER_FLOW)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"UseCount Underflow Error!");
		return;
	}
}

void Socket::RecvProcess(SessionInfo* sessionInfo)
{
	WORD length = 0;
	DWORD returnVal = 0;
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);

	while (sessionInfo->recvRingBuffer->GetUseSize() >= sizeof(header))
	{
		// ��� ��Ŷ üũ �� ���
		returnVal = sessionInfo->recvRingBuffer->Peek((char*)&header, sizeof(header));

		if (returnVal != sizeof(header))
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Header Peek size Error [returnVal:%d]", returnVal);
			return;
		}

		if (header.code != PACKET_CODE)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Header unknown code error![code:%d]", header.code);
			return;
		}

		//����� ���̷ε� ����� ��ģ ������� ������ ���� ������ �� �� ����. ������ ó�� ����
		if (header.payLoadSize + sizeof(header) > sessionInfo->recvRingBuffer->GetUseSize())
		{
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"payloadSize error![payLoadSize:%d]", header.payLoadSize);
			return;
		}

		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sessionID:%d] Header Type:%s RingBuf useSize:%d", 
			sessionInfo->sessionID,
			mHeaderNameData[header.msgType].c_str(),
			sessionInfo->recvRingBuffer->GetUseSize());

		sessionInfo->recvRingBuffer->MoveReadPos(returnVal);

		// ��Ŷ���ۿ� payload �Է�
		packetBuffer.Clear();
		returnVal = sessionInfo->recvRingBuffer->Peek(packetBuffer.GetBufferPtr(), header.payLoadSize);

		if (returnVal != header.payLoadSize)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Peek pay LoadSize  size Error [returnVal:%d][payloadSize:%d]", 
				returnVal, header.payLoadSize);
			return;
		}
		// ��Ŷ ���۵� ���ۿ� �������� �κ��̱� ������ writepos�� ���� �̵������ش�.
		packetBuffer.MoveWritePos(returnVal);

		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"payLoadSize:%d RingBuf useSize:%d", 
			returnVal, sessionInfo->recvRingBuffer->GetUseSize());

		sessionInfo->recvRingBuffer->MoveReadPos(returnVal);

		PacketProcess(header.msgType, sessionInfo, packetBuffer);
	}
}

void Socket::PacketProcess(const WORD msgType, const SessionInfo* sessionInfo, PacketBuffer& packetBuffer)
{
	BYTE direction = 0;
	short x = 0;
	short y = 0;

	switch (msgType)
	{
	case HEADER_CS_MOVE_START:
		{
			MoveStartRequest(sessionInfo, packetBuffer);
		}
		break;
	case HEADER_CS_MOVE_STOP:
		{
			MoveStopRequest(sessionInfo, packetBuffer);
		}
		break;
	case HEADER_CS_ATTACK1:
		{
			AttackRequest(sessionInfo, packetBuffer, 1);
		}
		break;
	case HEADER_CS_ATTACK2:
		{
			AttackRequest(sessionInfo, packetBuffer, 2);
		}
		break;
	case HEADER_CS_ATTACK3:
		{
			AttackRequest(sessionInfo, packetBuffer, 3);
		}
		break;
	default:
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"unknown msgType[%d]", msgType);
		return;
	}

}

void Socket::SendUnicast(const SessionInfo* sessionInfo, const HeaderInfo* header, const PacketBuffer& packetBuffer)
{
	sessionInfo->sendRingBuffer->Enqueue((char*)header, sizeof(HeaderInfo));
	sessionInfo->sendRingBuffer->Enqueue(packetBuffer.GetBufferPtr(), packetBuffer.GetDataSize());

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[SessionID:%d] Send Header Type:%s RingBuf useSize:%d",
		sessionInfo->sessionID,
		mHeaderNameData[header->msgType].c_str(),
		sessionInfo->sendRingBuffer->GetUseSize());
}

void Socket::SendBroadcast(const SessionInfo* sessionInfo, const HeaderInfo* header, const PacketBuffer& packetBuffer, const bool isSelf)
{
	for (auto iterSessionData : mSessionData)
	{
		if (isSelf == false)
		{
			if (iterSessionData.second == sessionInfo)
				continue;
		}
		SendUnicast(iterSessionData.second, header, packetBuffer);
	}
}

void Socket::SendSector_Broadcast(const DWORD sessionID, const int sectorX, const int sectorY, const HeaderInfo& header, const PacketBuffer& packetBuffer, const bool isSelf)
{
	for (ClientInfo* clientInfo : mSectorData[sectorY][sectorX])
	{
		if (isSelf == false)
		{
			if (clientInfo->sessionInfo->sessionID == sessionID)
				continue;
		}
		SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
	}
}

void Socket::SendAroundSector_Broadcast(const DWORD sessionID, const int sectorX, const int sectorY, const HeaderInfo& header, const PacketBuffer& packetBuffer, const bool isSelf)
{
	SectorAroundInfo mySector;

	GetSectorAround(sectorX, sectorY, &mySector);

	for (int i = 0; i < mySector.count; i++)
	{
		SendSector_Broadcast(sessionID,
			mySector.arround[i].x, mySector.arround[i].y, header, packetBuffer, isSelf);
	}

}

void Socket::MoveStartRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer)
{
	BYTE direction;
	short x;
	short y;
	ClientInfo* clientInfo = nullptr;
	HeaderInfo header;

	packetBuffer >> direction;
	packetBuffer >> x;
	packetBuffer >> y;

	clientInfo = FindClientInfo(sessionInfo->sessionID);
	if (clientInfo == nullptr)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"ClientData Find Error[ID:%d]",
			sessionInfo->sessionID);
		_ASSERT(false);
		return;
	}
	
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"MoveStart SessionID:%d / dir:%d / x: %d / y:%d",
		sessionInfo->sessionID, direction, x, y);

	// Ŭ���̾�Ʈ�� ������ ĳ���� ��ǥ ���̰� ���� �̻��̸� ���� ó������.
	if (abs(x - clientInfo->x) >= ERROR_RANGE || abs(y - clientInfo->y) >= ERROR_RANGE)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"ERROR_RANGE SessionID:%d / server_x: %d / server_y:%d / client_x: %d / client_y: %d",
			sessionInfo->sessionID, clientInfo->x, clientInfo->y, x, y);
		x = clientInfo->x;
		y = clientInfo->y;
		SyncMakePacket(&header, &packetBuffer, clientInfo);
		SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
			header, packetBuffer);
		//return;
		//_ASSERT(false);
	}

	clientInfo->action = direction;			// ���� ����
	clientInfo->moveDirection = direction;	// 8���� ����
	switch (direction)
	{
	case ACTION_MOVE_RR:
	case ACTION_MOVE_RU:
	case ACTION_MOVE_RD:
		{
			clientInfo->direction = ACTION_MOVE_RR;
		}
		break;
	case ACTION_MOVE_LL:
	case ACTION_MOVE_LU:
	case ACTION_MOVE_LD:
		{
			clientInfo->direction = ACTION_MOVE_LL;
		}
		break;
	case ACTION_MOVE_UU:
	case ACTION_MOVE_DD:
		break;
	default:
		{
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"unknown direction:%d", direction);
		}
		return;
	}
	clientInfo->isMove = true;

	//SectorUpdateCharcater(clientInfo, false);

	MoveStartMakePacket(&header, &packetBuffer, clientInfo);
	SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
		header, packetBuffer);

	/*SendSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
		header, packetBuffer);*/
	//MoveStartMakePacket(clientInfo, packetBuffer);
}

void Socket::MoveStartMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo)
{
	outPacketBuffer->Clear();
	*outPacketBuffer << clientInfo->sessionInfo->sessionID;
	*outPacketBuffer << clientInfo->moveDirection;
	*outPacketBuffer << clientInfo->x;
	*outPacketBuffer << clientInfo->y;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_MOVE_START;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::MoveStopRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer)
{
	BYTE direction;
	short x;
	short y;
	ClientInfo* clientInfo = nullptr;
	HeaderInfo header;

	packetBuffer >> direction;
	packetBuffer >> x;
	packetBuffer >> y;

	clientInfo = FindClientInfo(sessionInfo->sessionID);
	if (clientInfo == nullptr)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"ClientData Find Error[ID:%d]",
			sessionInfo->sessionID);
		return;
	}

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"MoveStop SessionID:%d / dir:%d / x: %d / y:%d",
		sessionInfo->sessionID, direction, x, y);

	// Ŭ���̾�Ʈ�� ������ ĳ���� ��ǥ ���̰� ���� �̻��̸� ���� ó������.
	if (abs(x - clientInfo->x) >= ERROR_RANGE || abs(y - clientInfo->y) >= ERROR_RANGE)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"ERROR_RANGE SessionID:%d / server_x: %d / server_y:%d / client_x: %d / client_y: %d",
			sessionInfo->sessionID, clientInfo->x, clientInfo->y, x, y);
		x = clientInfo->x;
		y = clientInfo->y;

		SyncMakePacket(&header, &packetBuffer, clientInfo);
		SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
			header, packetBuffer);
		//return;

		//_ASSERT(false);
	}

	if (clientInfo->direction != direction)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"direction uncorrect! SessionID:%d / server_dir: %d / client_dir:%d",
			sessionInfo->sessionID, clientInfo->direction, direction);
		_ASSERT(false);
		direction = clientInfo->direction;
	}
	
	clientInfo->isMove = false;

	MoveStopMakePacket(&header, &packetBuffer, clientInfo);
	SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
		header, packetBuffer);
}

void Socket::MoveStopMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo)
{
	outPacketBuffer->Clear();
	*outPacketBuffer << clientInfo->sessionInfo->sessionID;
	*outPacketBuffer << clientInfo->direction;
	*outPacketBuffer << clientInfo->x;
	*outPacketBuffer << clientInfo->y;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_MOVE_STOP;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::AttackRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer, const BYTE attackNum)
{
	BYTE direction;
	short x;
	short y;
	ClientInfo* clientInfo = nullptr;
	HeaderInfo header;

	packetBuffer >> direction;
	packetBuffer >> x;
	packetBuffer >> y;

	clientInfo = FindClientInfo(sessionInfo->sessionID);
	if (clientInfo == nullptr)
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"ClientData Find Error[ID:%d]",
			sessionInfo->sessionID);
		return;
	}
	// Ŭ���̾�Ʈ�� ������ ĳ���� ��ǥ ���̰� ���� �̻��̸� ���� ó������.
	if (abs(x - clientInfo->x) >= ERROR_RANGE || abs(y - clientInfo->y) >= ERROR_RANGE)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"ERROR_RANGE SessionID:%d / server_x: %d / server_y:%d / client_x: %d / client_y: %d",
			sessionInfo->sessionID, clientInfo->x, clientInfo->y, x, y);
		x = clientInfo->x;
		y = clientInfo->y;

		SyncMakePacket(&header, &packetBuffer, clientInfo);
		SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
			header, packetBuffer);
		//return;

		//_ASSERT(false);
	}

	if (clientInfo->direction != direction)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"direction uncorrect! SessionID:%d / server_dir: %d / client_dir:%d",
			sessionInfo->sessionID, clientInfo->direction, direction);
		_ASSERT(false);
		direction = clientInfo->direction;
	}

	clientInfo->isMove = false;
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"Attack_%d SessionID:%d / dir:%d / x: %d / y:%d",
		attackNum, sessionInfo->sessionID, direction, x, y);

	AttackCollision(clientInfo, attackNum);
	AttackMakePacket(&header, &packetBuffer, clientInfo, attackNum);
	SendAroundSector_Broadcast(clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y,
		header, packetBuffer);
}

void Socket::AttackMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo, const BYTE attackNum)
{
	outPacketBuffer->Clear();
	*outPacketBuffer << clientInfo->sessionInfo->sessionID;
	*outPacketBuffer << clientInfo->direction;
	*outPacketBuffer << clientInfo->x;
	*outPacketBuffer << clientInfo->y;

	outHeader->code = PACKET_CODE;
	switch (attackNum)
	{
	case 1:
		outHeader->msgType = HEADER_SC_ATTACK1;
		break;
	case 2:
		outHeader->msgType = HEADER_SC_ATTACK2;
		break;
	case 3:
		outHeader->msgType = HEADER_SC_ATTACK3;
		break;
	default:
		_ASSERT(false);
		return;
	}
	
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::AttackCollision(const ClientInfo* clientInfo, const BYTE attackNum)
{
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);

	int mySectorX = clientInfo->curSectorPos.x;
	int mySectorY = clientInfo->curSectorPos.y;
	int attackSectorX = 0;
	bool isAttackSection = true;
	BYTE attackDamage = 0;

	// ���⿡ ���� ������ Ȥ�� ���� ���͸� �߰��� �˻��Ѵ�. ������ ���� �浹üũ�� �ȵǴ� ��� ����
	switch (clientInfo->direction)
	{
	case ACTION_MOVE_LL:
		{
			attackSectorX = mySectorX - 1;
			if (attackSectorX < 0)
				isAttackSection = false;
		}
		break;
	case ACTION_MOVE_RR:
		{
			attackSectorX = mySectorX + 1;
			if (attackSectorX >= SECTOR_MAX_X)
				isAttackSection = false;
		}
		break;
	default:
		_ASSERT(false);
		break;
	}

	switch (attackNum)
	{
	case 1:
		attackDamage = 1;
		break;
	case 2:
		attackDamage = 5;
		break;
	case 3:
		attackDamage = 10;
		break;
	default:
		_ASSERT(false);
		return;
	}

	for (ClientInfo* _clientInfo : mSectorData[mySectorY][mySectorX])
	{
		if (clientInfo == _clientInfo)
			continue;

		if (CalDistance(clientInfo->x, clientInfo->y, _clientInfo->x, _clientInfo->y) <= ATTACK_RANGE)
		{
			_clientInfo->hp -= attackDamage;
			if (_clientInfo->hp <= 0)
				_clientInfo->hp = 0;

			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"MySector Damage AttackID:%d VictimID:%d VictimHP:%d",
				clientInfo->sessionInfo->sessionID, _clientInfo->sessionInfo->sessionID, _clientInfo->hp);

			DamageMakePacket(&header, &packetBuffer, clientInfo->sessionInfo->sessionID, 
				_clientInfo->sessionInfo->sessionID, _clientInfo->hp);
			SendAroundSector_Broadcast(_clientInfo->sessionInfo->sessionID, _clientInfo->curSectorPos.x, _clientInfo->curSectorPos.y,
				header, packetBuffer, true);
			return;
		}
	}

	if (isAttackSection == true)
	{
		for (ClientInfo* _clientInfo : mSectorData[mySectorY][attackSectorX])
		{
			if (clientInfo == _clientInfo)
				continue;

			if (CalDistance(clientInfo->x, clientInfo->y, _clientInfo->x, _clientInfo->y) <= ATTACK_RANGE)
			{
				_clientInfo->hp -= attackDamage;
				if (_clientInfo->hp <= 0)
					_clientInfo->hp = 0;

				DamageMakePacket(&header, &packetBuffer, clientInfo->sessionInfo->sessionID,
					_clientInfo->sessionInfo->sessionID, _clientInfo->hp);
				SendAroundSector_Broadcast(_clientInfo->sessionInfo->sessionID, _clientInfo->curSectorPos.x, _clientInfo->curSectorPos.y,
					header, packetBuffer, true);

				CONSOLE_LOG(LOG_LEVEL_DEBUG, L"DirSector Damage AttackID:%d VictimID:%d VictimHP:%d",
					clientInfo->sessionInfo->sessionID, _clientInfo->sessionInfo->sessionID, _clientInfo->hp);
				return;
			}
		}
	}
}

void Socket::DamageMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const DWORD attackID, const DWORD victimID, const BYTE victimHP)
{
	outPacketBuffer->Clear();
	*outPacketBuffer << attackID;
	*outPacketBuffer << victimID;
	*outPacketBuffer << victimHP;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_DAMAGE;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::CreateCharacter_MakePacket(const ClientInfo* clientInfo)
{
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);

	packetBuffer << clientInfo->sessionInfo->sessionID;
	packetBuffer << clientInfo->direction;
	packetBuffer << clientInfo->x;
	packetBuffer << clientInfo->y;
	packetBuffer << clientInfo->hp;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_CREATE_MY_CHARACTER;
	header.payLoadSize = packetBuffer.GetDataSize();

	SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
}

//void Socket::CreateCharacterOther_MakePacket(const SessionInfo* sessionInfo)
//{
//	HeaderInfo header;
//	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
//	ClientInfo* clientInfo = nullptr;
//
//	for (auto iterClietData : mClientData)
//	{
//		clientInfo = iterClietData.second;
//
//		packetBuffer.Clear();
//		packetBuffer << clientInfo->sessionInfo->sessionID;
//		packetBuffer << clientInfo->direction;
//		packetBuffer << clientInfo->x;
//		packetBuffer << clientInfo->y;
//		packetBuffer << clientInfo->hp;
//
//		header.code = PACKET_CODE;
//		header.msgType = HEADER_SC_CREATE_OTHER_CHARACTER;
//		header.payLoadSize = packetBuffer.GetDataSize();
//
//		if (sessionInfo->sessionID == clientInfo->sessionInfo->sessionID)
//		{
//			SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer);
//		}
//		else
//		{
//			SendUnicast(sessionInfo, &header, packetBuffer);
//		}
//	}
//}

void Socket::CreateOtherCharacter_MakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo)
{
	outPacketBuffer->Clear();

	*outPacketBuffer << clientInfo->sessionInfo->sessionID;
	*outPacketBuffer << clientInfo->direction;
	*outPacketBuffer << clientInfo->x;
	*outPacketBuffer << clientInfo->y;
	*outPacketBuffer << clientInfo->hp;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_CREATE_OTHER_CHARACTER;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::RemoveCharacter_MakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo)
{
	outPacketBuffer->Clear();

	//PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
	*outPacketBuffer << clientInfo->sessionInfo->sessionID;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_DELETE_CHARACTER;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();

	//SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer, true);
}

void Socket::SyncMakePacket(HeaderInfo* outHeader, PacketBuffer* outPacketBuffer, const ClientInfo* clientInfo)
{
	outPacketBuffer->Clear();
	*outPacketBuffer << clientInfo->sessionInfo->sessionID;
	*outPacketBuffer << clientInfo->x;
	*outPacketBuffer << clientInfo->y;

	outHeader->code = PACKET_CODE;
	outHeader->msgType = HEADER_SC_SYNC;
	outHeader->payLoadSize = outPacketBuffer->GetDataSize();
}

void Socket::CharacterUpdate()
{
	ClientInfo* clientInfo = nullptr;
	unordered_map<DWORD, ClientInfo*>::iterator iterClientData;

	for (iterClientData = mClientData.begin();
		iterClientData != mClientData.end();)
	{
		clientInfo = iterClientData->second;

		// ĳ���� ��� ó�� üũ
		if (clientInfo->hp <= 0)
		{
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[SessionID:%d] Dead", clientInfo->sessionInfo->sessionID);
			iterClientData = RemoveSessionInfo(clientInfo->sessionInfo->sessionID);
		}
		else
		{
			ActionProc(clientInfo);
			++iterClientData;
		}	
	}
}

void Socket::ActionProc(ClientInfo* clientInfo)
{
	// ĳ���� �̵� ����
	bool isMove = false;
	short curX = clientInfo->x;
	short curY = clientInfo->y;

	if (clientInfo->isMove == false)
		return;

	switch (clientInfo->action)
	{
	case ACTION_MOVE_LL:
		{
			isMove = MoveCurX(&curX, true);

			if (isMove == true)
				clientInfo->x = curX;
		}
		break;
	case ACTION_MOVE_LU:
		{
			// X, Y��ǥ�̵��� �����ؾ� ��Ŷ ���� �� ������ ����
			if (true == MoveCurX(&curX, true) && true == MoveCurY(&curY, true))
			{
				isMove = true;
				clientInfo->x = curX;
				clientInfo->y = curY;
			}
			else
			{
				isMove = false;
			}
		}
		break;
	case ACTION_MOVE_LD:
		{
			// X, Y��ǥ�̵��� �����ؾ� ��Ŷ ���� �� ������ ����
			if (true == MoveCurX(&curX, true) && true == MoveCurY(&curY, false))
			{
				isMove = true;
				clientInfo->x = curX;
				clientInfo->y = curY;
			}
			else
			{
				isMove = false;
			}
		}
		break;
	case ACTION_MOVE_UU:
		{
			isMove = MoveCurY(&curY, true);

			if (isMove == true)
				clientInfo->y = curY;
		}
		break;
	case ACTION_MOVE_RU:
		{
			// X, Y��ǥ�̵��� �����ؾ� ��Ŷ ���� �� ������ ����
			if (true == MoveCurX(&curX, false) && true == MoveCurY(&curY, true))
			{
				isMove = true;
				clientInfo->x = curX;
				clientInfo->y = curY;
			}
			else
			{
				isMove = false;
			}
		}
		break;
	case ACTION_MOVE_RR:
		{
			isMove = MoveCurX(&curX, false);

			if (isMove == true)
				clientInfo->x = curX;
		}
		break;
	case ACTION_MOVE_RD:
		{

			// X, Y��ǥ�̵��� �����ؾ� ��Ŷ ���� �� ������ ����
			if (true == MoveCurX(&curX, false) && true == MoveCurY(&curY, false))
			{
				isMove = true;
				clientInfo->x = curX;
				clientInfo->y = curY;
			}
			else
			{
				isMove = false;
			}
		}
		break;
	case ACTION_MOVE_DD:
		{
			isMove = MoveCurY(&curY, false);

			if (isMove == true)
				clientInfo->y = curY;
		}
		break;
	default:
		return;
	}

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"Move SessionID:%d server_x:%d server_y:%d",
		clientInfo->sessionInfo->sessionID, clientInfo->x, clientInfo->y);

	// �ش� ĳ���� ���� ����
	if(isMove == true)
		SectorUpdateCharcater(clientInfo, false);
}

bool Socket::MoveCurX(short* outCurX, const bool isLeft)
{
	if (isLeft == true)
		*outCurX -= MOVE_X_PIXEL;
	else
		*outCurX += MOVE_X_PIXEL;

	if (*outCurX <= RANGE_MOVE_LEFT + MOVE_X_PIXEL)
	{
		*outCurX = RANGE_MOVE_LEFT + MOVE_X_PIXEL;
		return false;
	}
	else if (*outCurX >= RANGE_MOVE_RIGHT - MOVE_X_PIXEL)
	{
		*outCurX = RANGE_MOVE_RIGHT - MOVE_X_PIXEL;
		return false;
	}

	return true;
}

bool Socket::MoveCurY(short* outCurY, const bool isUp)
{
	if (isUp == true)
		*outCurY -= MOVE_Y_PIXEL;
	else
		*outCurY += MOVE_Y_PIXEL;

	if (*outCurY < RANGE_MOVE_TOP + MOVE_Y_PIXEL)
	{
		*outCurY = RANGE_MOVE_TOP + MOVE_Y_PIXEL;
		return false;
	}
	else if (*outCurY > RANGE_MOVE_BOTTOM - MOVE_Y_PIXEL)
	{
		*outCurY = RANGE_MOVE_BOTTOM - MOVE_Y_PIXEL;
		return false;
	}

	return true;
}

void Socket::AddSessionInfo(const SOCKET clientSock, SOCKADDR_IN& clientAddr)
{
	WCHAR clientIP[IP_BUFFER_SIZE] = { 0, };

	SessionInfo* sessionInfo = nullptr;

	sessionInfo = new SessionInfo;
	_ASSERT(sessionInfo != nullptr);

	InetNtop(AF_INET, &clientAddr.sin_addr, clientIP, 16);
	wprintf(L"\n[CHAT SERVER] Client IP: %s Clinet Port:%d\n", clientIP, ntohs(clientAddr.sin_port));

	sessionInfo->clientSock = clientSock;
	wcscpy_s(sessionInfo->ip, _countof(sessionInfo->ip), clientIP);
	sessionInfo->port = ntohs(clientAddr.sin_port);
	sessionInfo->sessionID = ++mSession_IDNum;
	sessionInfo->recvRingBuffer = new RingBuffer;
	sessionInfo->sendRingBuffer = new RingBuffer;

	mSessionData.emplace(sessionInfo->sessionID, sessionInfo);
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sessionID:%d] session insert size:%d", sessionInfo->sessionID, mSessionData.size());
	AddClientInfo(sessionInfo);
}

unordered_map<DWORD, Socket::ClientInfo*>::iterator Socket::RemoveSessionInfo(const DWORD sessionID)
{
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
	ClientInfo* clientInfo = nullptr;
	unordered_map<DWORD, Socket::ClientInfo*>::iterator iterClientData;

	// ���� ������ ����
	iterClientData = mClientData.find(sessionID);
	if (iterClientData == mClientData.end())
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"RemoveClientInfo clientData find error![sessionID:%d]", sessionID);
		return iterClientData;
	}
	clientInfo = iterClientData->second;
	_ASSERT(clientInfo != nullptr);

	// �ٸ� ������� ������ ĳ���� ���� ��Ŷ ����
	RemoveCharacter_MakePacket(&header, &packetBuffer, clientInfo);

	// �������� ����
	SendAroundSector_Broadcast(sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y, header, packetBuffer);
	
	for (list<ClientInfo*>::iterator iterSectorData = mSectorData[clientInfo->curSectorPos.y][clientInfo->curSectorPos.x].begin();
		iterSectorData != mSectorData[clientInfo->curSectorPos.y][clientInfo->curSectorPos.x].end(); iterSectorData++)
	{
		if (*iterSectorData == clientInfo)
		{
			mSectorData[clientInfo->curSectorPos.y][clientInfo->curSectorPos.x].erase(iterSectorData);
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sector id:%d] sector erase / sectorX:%d / sectorY:%d",
				clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y);
			break;
		}
	}

	SafeDelete(clientInfo);
	iterClientData = mClientData.erase(iterClientData);

	// ���� ������ ����
	auto iterSessionData = mSessionData.find(sessionID);
	if (iterSessionData == mSessionData.end())
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"RemoveClientInfo session find error![sessionID:%d]", sessionID);
		return iterClientData;
	}
	closesocket(iterSessionData->second->clientSock);
	SafeDelete(iterSessionData->second->recvRingBuffer);
	SafeDelete(iterSessionData->second->sendRingBuffer);
	SafeDelete(iterSessionData->second);
	mSessionData.erase(iterSessionData);
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sessionID:%d] session erase size:%d", sessionID, mSessionData.size());
	return iterClientData;
}

void Socket::HeaderNameInsert()
{
	mHeaderNameData.emplace(10, L"HEADER_CS_MOVE_START");
	mHeaderNameData.emplace(12, L"HEADER_CS_MOVE_STOP");
	mHeaderNameData.emplace(20, L"HEADER_CS_ATTACK1");
	mHeaderNameData.emplace(21, L"HEADER_SC_ATTACK1");
	mHeaderNameData.emplace(22, L"HEADER_CS_ATTACK2");
	mHeaderNameData.emplace(23, L"HEADER_SC_ATTACK2");
	mHeaderNameData.emplace(24, L"HEADER_CS_ATTACK3");
	mHeaderNameData.emplace(25, L"HEADER_SC_ATTACK3");
	mHeaderNameData.emplace(0, L"HEADER_SC_CREATE_MY_CHARACTER");
	mHeaderNameData.emplace(1, L"HEADER_SC_CREATE_OTHER_CHARACTER");
	mHeaderNameData.emplace(2, L"HEADER_SC_DELETE_CHARACTER");
	mHeaderNameData.emplace(11, L"HEADER_SC_MOVE_START");
	mHeaderNameData.emplace(13, L"HEADER_SC_MOVE_STOP");
}

void Socket::AddClientInfo(SessionInfo* sessionInfo)
{
	ClientInfo* clientInfo = nullptr;
	clientInfo = new ClientInfo;
	_ASSERT(clientInfo != nullptr);

	clientInfo->hp = 100;
	clientInfo->sessionInfo = sessionInfo;
	clientInfo->x = 50;
	clientInfo->y = 40;
	mClientData.emplace(sessionInfo->sessionID, clientInfo);
	CreateCharacter_MakePacket(clientInfo);
	//CreateCharacterOther_MakePacket(clientInfo->sessionInfo);

	// �ش� ĳ���� ���� ����
	SectorUpdateCharcater(clientInfo, true);
}

Socket::ClientInfo* Socket::FindClientInfo(const DWORD sessionID)
{
	ClientInfo* clientInfo = nullptr;

	auto clientIter = mClientData.find(sessionID);
	if (clientIter == mClientData.end())
	{
		return nullptr;
	}

	clientInfo = clientIter->second;

	return clientInfo;
}

void Socket::SectorUpdateCharcater(ClientInfo* clientInfo, const bool isCreateCharacter)
{
	// ���� ���� 
	clientInfo->oldSectorPos = clientInfo->curSectorPos;
	clientInfo->curSectorPos.x = clientInfo->x / mSectorRange.x;
	clientInfo->curSectorPos.y = clientInfo->y / mSectorRange.y;

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sesssionID:%d] sectorX:%d sectorY:%d", 
		clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y);

	if (clientInfo->curSectorPos.y < 0 || clientInfo->curSectorPos.y >= SECTOR_MAX_Y ||
		clientInfo->curSectorPos.x < 0 || clientInfo->curSectorPos.x >= SECTOR_MAX_X)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sesssionID:%d] sector Error");
		return;
	}
	
	// ���� ĳ���� ������ �ƴϸ� �ߺ��ؼ� ���� �����Ϳ� ĳ���Ͱ� �߰��Ǵ°��� ����
	if (isCreateCharacter == true)
	{
		// ���ο� ���Ϳ� �ش� ĳ���� ����
		mSectorData[clientInfo->curSectorPos.y][clientInfo->curSectorPos.x].emplace_back(clientInfo);
		// ���Ͱ� ����Ǿ��� ������ �߰� �� ������ ���� ����Ͽ� ���� ��Ŷ ����
		CreateSectorMyAroundPacket(clientInfo);
		return;
	}
	else
	{
		if (clientInfo->oldSectorPos.x == clientInfo->curSectorPos.x &&
			clientInfo->oldSectorPos.y == clientInfo->curSectorPos.y)
			return;
	}


	// ���ο� ���Ϳ� �ش� ĳ���� ����
	mSectorData[clientInfo->curSectorPos.y][clientInfo->curSectorPos.x].emplace_back(clientInfo);
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sector id:%d] sector insert / sectorX:%d / sectorY:%d", 
		clientInfo->sessionInfo->sessionID, clientInfo->curSectorPos.x, clientInfo->curSectorPos.y);
	// ������ �ִ� ���� ĳ���� ����
	for (list<ClientInfo*>::iterator iterSectorData = mSectorData[clientInfo->oldSectorPos.y][clientInfo->oldSectorPos.x].begin();
		iterSectorData != mSectorData[clientInfo->oldSectorPos.y][clientInfo->oldSectorPos.x].end(); iterSectorData++)
	{
		if (*iterSectorData == clientInfo)
		{
			mSectorData[clientInfo->oldSectorPos.y][clientInfo->oldSectorPos.x].erase(iterSectorData);
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"[sector id:%d] sector erase / sectorX:%d / sectorY:%d",
				clientInfo->sessionInfo->sessionID, clientInfo->oldSectorPos.x, clientInfo->oldSectorPos.y);
			break;
		}
	}

	// ���Ͱ� ����Ǿ��� ������ �߰� �� ������ ���� ����Ͽ� ���� ��Ŷ ����
	SectorUpdatePacket(clientInfo);
	
}

void Socket::GetSectorAround(int sectorX, int sectorY, SectorAroundInfo* sectorAround)
{
	--sectorX;
	--sectorY;

	sectorAround->count = 0;
	for (int y = 0; y < 3; ++y)
	{
		if (sectorY + y < 0 || sectorY + y >= SECTOR_MAX_Y)
			continue;

		for (int x = 0; x < 3; ++x)
		{
			if (sectorX + x < 0 || sectorX + x >= SECTOR_MAX_X)
				continue;

			sectorAround->arround[sectorAround->count].x = sectorX + x;
			sectorAround->arround[sectorAround->count].y = sectorY + y;
			++sectorAround->count;
		}
	}
}

void Socket::GetUpdateSectorAround(ClientInfo* clientInfo, SectorAroundInfo* outRemoveSector, SectorAroundInfo* outAddSector)
{
	bool isFind;
	SectorAroundInfo oldSector;
	SectorAroundInfo curSector;

	oldSector.count = 0;
	curSector.count = 0;
	outRemoveSector->count = 0;
	outAddSector->count = 0;

	GetSectorAround(clientInfo->oldSectorPos.x, clientInfo->oldSectorPos.y, &oldSector);
	GetSectorAround(clientInfo->curSectorPos.x, clientInfo->curSectorPos.y, &curSector);

	// ����(OldSector) ���� ���� ��, �űԼ���(AddSector) ���� ���� ������ ã�Ƽ� RemoveSector�� ����
	for (int oldCount = 0; oldCount < oldSector.count; oldCount++)
	{
		isFind = false;
		for(int curCount = 0; curCount < curSector.count; curCount++)
		{
			if (oldSector.arround[oldCount].x == curSector.arround[curCount].x &&
				oldSector.arround[oldCount].y == curSector.arround[curCount].y)
			{
				isFind = true;
				break;
			}
		}
		if (isFind == false)
		{
			outRemoveSector->arround[outRemoveSector->count] = oldSector.arround[oldCount];
			outRemoveSector->count++;
		}
	}

	// ����(CurSector) ���� ������ ���� (OldSector) ���Ϳ� ���� ������ ã�Ƽ� AddSector�� ����
	for (int curCount = 0; curCount < curSector.count; curCount++)
	{
		isFind = false;
		for (int oldCount = 0; oldCount < oldSector.count; oldCount++)
		{
			if (oldSector.arround[oldCount].x == curSector.arround[curCount].x &&
				oldSector.arround[oldCount].y == curSector.arround[curCount].y)
			{
				isFind = true;
				break;
			}
		}
		if (isFind == false)
		{
			outAddSector->arround[outAddSector->count] = curSector.arround[curCount];
			outAddSector->count++;
		}
	}
}

void Socket::SectorUpdatePacket(ClientInfo* clientInfo)
{
	SectorAroundInfo addSector;
	SectorAroundInfo removeSector;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
	HeaderInfo header;
	list<ClientInfo*>::iterator iterClientInfo;

	GetUpdateSectorAround(clientInfo, &removeSector, &addSector);

	//RemoveSector�� ���ڽ� ĳ���� ���� ������ ������Ű��
	RemoveCharacter_MakePacket(&header, &packetBuffer, clientInfo);
	for (int i = 0; i < removeSector.count; i++)
	{
		SendSector_Broadcast(clientInfo->sessionInfo->sessionID, 
			removeSector.arround[i].x, removeSector.arround[i].y, header, packetBuffer);
	}

	//RemoveSector�� ĳ���� ���������� ������ ������
	for (int i = 0; i < removeSector.count; i++)
	{
		for (auto _clientInfo : mSectorData[removeSector.arround[i].y][removeSector.arround[i].x])
		{
			// �ڱ⽺���ο��� ������ ����
			if (_clientInfo == clientInfo)
				continue;

			RemoveCharacter_MakePacket(&header, &packetBuffer, _clientInfo);
			SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
		}
	}

	//AddSector�� ���ڽ� ĳ���� ���� ���� ������
	CreateOtherCharacter_MakePacket(&header, &packetBuffer, clientInfo);
	for (int i = 0; i < addSector.count; i++)
	{
		SendSector_Broadcast(clientInfo->sessionInfo->sessionID,
			addSector.arround[i].x, addSector.arround[i].y, header, packetBuffer);
	}

	//AddSector�� ĳ���� ���������� ������ ������
	for (int i = 0; i < addSector.count; i++)
	{
		for (auto _clientInfo : mSectorData[addSector.arround[i].y][addSector.arround[i].x])
		{
			// �ڱ⽺���ο��� ������ ����
			if (_clientInfo == clientInfo)
				continue;

			CreateOtherCharacter_MakePacket(&header, &packetBuffer, _clientInfo);
			SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
		}
	}


	//AddSector�� ���ڽ� ĳ���� ������ ���� ������
	MoveStartMakePacket(&header, &packetBuffer, clientInfo);
	for (int i = 0; i < addSector.count; i++)
	{
		SendSector_Broadcast(clientInfo->sessionInfo->sessionID,
			addSector.arround[i].x, addSector.arround[i].y, header, packetBuffer);
	}

	//AddSector�� ĳ���� ������ ������ ������ ������
	for (int i = 0; i < addSector.count; i++)
	{
		for (auto _clientInfo : mSectorData[addSector.arround[i].y][addSector.arround[i].x])
		{
			// �ڱ⽺���ο��� ������ ����
			if (_clientInfo == clientInfo)
				continue;
			if (_clientInfo->isMove == true)
			{
				MoveStartMakePacket(&header, &packetBuffer, _clientInfo);
				SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
			}
		}
	}
}

void Socket::CreateSectorMyAroundPacket(ClientInfo* clientInfo)
{
	SectorAroundInfo mySector;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
	HeaderInfo header;

	GetSectorAround(clientInfo->curSectorPos.x, clientInfo->curSectorPos.y, &mySector);

	//���ֺ� ���Ϳ� ���ڽ� ĳ���� ���� ���� ������
	CreateOtherCharacter_MakePacket(&header, &packetBuffer, clientInfo);
	for (int i = 0; i < mySector.count; i++)
	{
		SendSector_Broadcast(clientInfo->sessionInfo->sessionID,
			mySector.arround[i].x, mySector.arround[i].y, header, packetBuffer);
	}

	//���ֺ� ������ ĳ���� ���������� ������ ������
	for (int i = 0; i < mySector.count; i++)
	{
		for (auto _clientInfo : mSectorData[mySector.arround[i].y][mySector.arround[i].x])
		{
			if (clientInfo == _clientInfo)
				continue;

			CreateOtherCharacter_MakePacket(&header, &packetBuffer, _clientInfo);
			SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);

			if (_clientInfo->isMove == true)
			{
				MoveStartMakePacket(&header, &packetBuffer, _clientInfo);
				SendUnicast(clientInfo->sessionInfo, &header, packetBuffer);
			}
		}
	}
}


