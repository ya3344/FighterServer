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
	int returnValue = 0;

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

	// 논블록킹 소켓으로 전환
	u_long on = 1;
	if (SOCKET_ERROR == ioctlsocket(mListenSock, FIONBIO, &on))
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"ioctlsocket() errcode[%d]", WSAGetLastError());
		return false;
	}

	HeaderNameInsert();
	// packetBuffer 할당
	/*mPacketBuffer = new PacketBuffer;
	_ASSERT(mPacketBuffer != nullptr);
	mPacketBuffer->Initialize();*/

	return true;
}

bool Socket::ServerProcess()
{
	fd_set read_set;
	fd_set write_set;
	SessionInfo* sessionInfo = nullptr;
	BYTE socketCount = 0;
	vector<DWORD> sessionID_Data;

	// userid 는 최대 fdsetsize 만큼 들어가기 때문에 미리 메모리 할당 하여 reallocation 방지
	sessionID_Data.reserve(FD_SETSIZE);

	while (true)
	{
		// 소켓 셋 초기화
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		// 리슨 소켓 셋
		FD_SET(mListenSock, &read_set);

		// 유저 데이터 초기화
		sessionID_Data.clear();
		socketCount = 0;

		// 클라이언트 소켓 셋
		for (auto sessionData : mSessionData)
		{
			sessionInfo = sessionData.second;
			FD_SET(sessionInfo->clientSock, &read_set);
			sessionID_Data.emplace_back(sessionInfo->sessionID);

			if (sessionInfo->sendRingBuffer->GetUseSize() > 0)
			{
				FD_SET(sessionInfo->clientSock, &write_set);
			}

			// 64개씩 끊어서 select 실행
			if (socketCount >= FD_SETSIZE)
			{
				socketCount = 0;
				SelectSocket(read_set, write_set, sessionID_Data);

				// 소켓 셋 초기화
				FD_ZERO(&read_set);
				FD_ZERO(&write_set);
				// 소켓 리스트 초기화
				sessionID_Data.clear();
				// 리슨 소켓 셋
				FD_SET(mListenSock, &read_set);
			}
			else
			{
				++socketCount;
			}
		}
		// 64개 미만인 select 실행
		SelectSocket(read_set, write_set, sessionID_Data);

		// Update 처리
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
		//20ms를 초과 X -> Sleep 한 시간만큼 old에 더한다. 
		//20ms를 초과 O -> 초과한 시간만큼 old에서 뺀다. (그냥 누적하다가 1프레임이 넘으면 스킵) old = cur - (deltaTime - 20);
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

	// select 즉시 리턴
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
	// 소켓 셋 검사
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

		// 세션 정보 추가
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
					// 강제 연결 접속 종료
					if (WSAGetLastError() == WSAECONNRESET)
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

	// 링버퍼의 최대사이즈를 보냄
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
		// WSAEWOULDBLOCK 상태이기 때문에 에러는 아니므로 바로 리턴
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
		// 헤더 패킷 체크 및 출력
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

		//헤더와 페이로드 사이즈가 합친 사이즈보다 적으면 다음 수행을 할 수 없다. 다음에 처리 진행
		if (header.payLoadSize + sizeof(header) > sessionInfo->recvRingBuffer->GetUseSize())
		{
			CONSOLE_LOG(LOG_LEVEL_DEBUG, L"payloadSize error![payLoadSize:%d]", header.payLoadSize);
			return;
		}

		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"Header Type:%s RingBuf useSize:%d", 
			mHeaderNameData[header.msgType].c_str(),
			sessionInfo->recvRingBuffer->GetUseSize());

		sessionInfo->recvRingBuffer->MoveReadPos(returnVal);

		// 패킷버퍼에 payload 입력
		packetBuffer.Clear();
		returnVal = sessionInfo->recvRingBuffer->Peek(packetBuffer.GetBufferPtr(), header.payLoadSize);

		if (returnVal != header.payLoadSize)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Peek pay LoadSize  size Error [returnVal:%d][payloadSize:%d]", 
				returnVal, header.payLoadSize);
			return;
		}
		// 패킷 버퍼도 버퍼에 직접담은 부분이기 때문에 writepos을 직접 이동시켜준다.
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
			
		}
		break;
	case HEADER_CS_ATTACK2:
		{
			
		}
		break;
	case HEADER_CS_ATTACK3:
		{
			
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

	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"Send Header Type:%s RingBuf useSize:%d",
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

void Socket::MoveStartRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer)
{
	BYTE direction;
	short x;
	short y;
	ClientInfo* clientInfo = nullptr;

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

	// 클라이언트와 서버의 캐릭터 좌표 차이가 범위 이상이면 에러 처리진행.
	if (abs(x - clientInfo->x) >= ERROR_RANGE || abs(y - clientInfo->y) >= ERROR_RANGE)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"ERROR_RANGE SessionID:%d / server_x: %d / server_y:%d / client_x: %d / client_y: %d",
			sessionInfo->sessionID, clientInfo->x, clientInfo->y, x, y);
		x = clientInfo->x;
		y = clientInfo->y;
		_ASSERT(false);
	}

	clientInfo->action = direction;			// 동작 변경
	clientInfo->moveDirection = direction;	// 8방향 적용
	switch (direction)
	{
	case ACTION_MOVE_RR:
	case ACTION_MOVE_RU:
	case ACTION_MOVE_RD:
		{
			clientInfo->direction = HEADER_MOVE_DIR_RR;
		}
		break;
	case ACTION_MOVE_LL:
	case ACTION_MOVE_LU:
	case ACTION_MOVE_LD:
		{
			clientInfo->direction = HEADER_MOVE_DIR_LL;
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
	MoveStartMakePacket(clientInfo, packetBuffer);
}

void Socket::MoveStartMakePacket(const ClientInfo* clientInfo, PacketBuffer& packetBuffer)
{
	HeaderInfo header;

	packetBuffer.Clear();
	packetBuffer << clientInfo->sessionInfo->sessionID;
	packetBuffer << clientInfo->moveDirection;
	packetBuffer << clientInfo->x;
	packetBuffer << clientInfo->y;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_MOVE_START;
	header.payLoadSize = packetBuffer.GetDataSize();

	SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer);
}

void Socket::MoveStopRequest(const SessionInfo* sessionInfo, PacketBuffer& packetBuffer)
{
	BYTE direction;
	short x;
	short y;
	ClientInfo* clientInfo = nullptr;

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

	// 클라이언트와 서버의 캐릭터 좌표 차이가 범위 이상이면 에러 처리진행.
	if (abs(x - clientInfo->x) >= ERROR_RANGE || abs(y - clientInfo->y) >= ERROR_RANGE)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"ERROR_RANGE SessionID:%d / server_x: %d / server_y:%d / client_x: %d / client_y: %d",
			sessionInfo->sessionID, clientInfo->x, clientInfo->y, x, y);
		x = clientInfo->x;
		y = clientInfo->y;
		_ASSERT(false);
	}

	if (clientInfo->direction != direction)
	{
		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"direction uncorrect! SessionID:%d / server_dir: %d / client_dir:%d",
			sessionInfo->sessionID, clientInfo->direction, direction);
		_ASSERT(false);
		direction = clientInfo->direction;
	}
	
	clientInfo->isMove = false;
	MoveStopMakePacket(clientInfo, packetBuffer);
}

void Socket::MoveStopMakePacket(const ClientInfo* clientInfo, PacketBuffer& packetBuffer)
{
	HeaderInfo header;

	packetBuffer.Clear();
	packetBuffer << clientInfo->sessionInfo->sessionID;
	packetBuffer << clientInfo->direction;
	packetBuffer << clientInfo->x;
	packetBuffer << clientInfo->y;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_MOVE_STOP;
	header.payLoadSize = packetBuffer.GetDataSize();

	SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer);
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

void Socket::CreateCharacterOther_MakePacket(const SessionInfo* sessionInfo)
{
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);
	ClientInfo* clientInfo = nullptr;

	for (auto iterClietData : mClientData)
	{
		clientInfo = iterClietData.second;

		packetBuffer.Clear();
		packetBuffer << clientInfo->sessionInfo->sessionID;
		packetBuffer << clientInfo->direction;
		packetBuffer << clientInfo->x;
		packetBuffer << clientInfo->y;
		packetBuffer << clientInfo->hp;

		header.code = PACKET_CODE;
		header.msgType = HEADER_SC_CREATE_OTHER_CHARACTER;
		header.payLoadSize = packetBuffer.GetDataSize();

		if (sessionInfo->sessionID == clientInfo->sessionInfo->sessionID)
		{
			SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer);
		}
		else
		{
			SendUnicast(sessionInfo, &header, packetBuffer);
		}
	}


	
}

void Socket::RemoveCharacter_MakePacket(const ClientInfo* clientInfo)
{
	HeaderInfo header;
	PacketBuffer packetBuffer(PacketBuffer::BUFFER_SIZE_DEFAULT);

	packetBuffer << clientInfo->sessionInfo->sessionID;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_DELETE_CHARACTER;
	header.payLoadSize = packetBuffer.GetDataSize();

	SendBroadcast(clientInfo->sessionInfo, &header, packetBuffer, true);
}

void Socket::CharacterUpdate()
{
	ClientInfo* clientInfo = nullptr;

	for (auto iterClientData : mClientData)
	{
		clientInfo = iterClientData.second;
		ActionProc(clientInfo);
	}
}

void Socket::ActionProc(ClientInfo* clientInfo)
{
	// 캐릭터 이동 관련
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
			// X, Y좌표이동이 가능해야 패킷 전송 및 움직임 진행
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
			// X, Y좌표이동이 가능해야 패킷 전송 및 움직임 진행
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
			// X, Y좌표이동이 가능해야 패킷 전송 및 움직임 진행
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

			// X, Y좌표이동이 가능해야 패킷 전송 및 움직임 진행
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
	AddClientInfo(sessionInfo);
}

void Socket::RemoveSessionInfo(const DWORD sessionID)
{
	// 유저 데이터 삭제
	auto iterClientData = mClientData.find(sessionID);
	if (iterClientData == mClientData.end())
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"RemoveClientInfo clientData find error![sessionID:%d]", sessionID);
		return;
	}
	// 다른 유저들과 나에게 캐릭터 삭제 패킷 전송
	RemoveCharacter_MakePacket(iterClientData->second);

	SafeDelete(iterClientData->second);
	mClientData.erase(iterClientData);

	// 세션 데이터 삭제
	auto iterSessionData = mSessionData.find(sessionID);
	if (iterSessionData == mSessionData.end())
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"RemoveClientInfo session find error![sessionID:%d]", sessionID);
		return;
	}
	closesocket(iterSessionData->second->clientSock);
	SafeDelete(iterSessionData->second->recvRingBuffer);
	SafeDelete(iterSessionData->second->sendRingBuffer);
	SafeDelete(iterSessionData->second);
	--mSession_IDNum;
	mSessionData.erase(iterSessionData);
}

void Socket::HeaderNameInsert()
{
	mHeaderNameData.emplace(10, L"HEADER_CS_MOVE_START");
	mHeaderNameData.emplace(12, L"HEADER_CS_MOVE_STOP");
	mHeaderNameData.emplace(20, L"HEADER_CS_ATTACK1");
	mHeaderNameData.emplace(22, L"HEADER_CS_ATTACK2");
	mHeaderNameData.emplace(24, L"HEADER_CS_ATTACK3");
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
	CreateCharacterOther_MakePacket(clientInfo->sessionInfo);
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
