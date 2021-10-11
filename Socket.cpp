#include "pch.h"
#include "Socket.h"
#include "../Common\RingBuffer/RingBuffer.h"
#include "../Common\PacketBuffer/PacketBuffer.h"

Socket::~Socket()
{
	closesocket(mListenSock);
	WSACleanup();
	SafeDelete(mPacketBuffer);
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

	// packetBuffer 할당
	mPacketBuffer = new PacketBuffer;
	_ASSERT(mPacketBuffer != nullptr);
	mPacketBuffer->Initialize();

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
	}

	return false;
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
				CONSOLE_LOG(LOG_LEVEL_DEBUG, L"recv packet size:%d", returnVal);

				if (returnVal == SOCKET_ERROR)
				{
					// 강제 연결 접속 종료
					if (WSAGetLastError() == WSAECONNRESET)
					{
						RemoveSessionInfo(sessionInfo);
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
				RemoveSessionInfo(sessionInfo);
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
	CONSOLE_LOG(LOG_LEVEL_DEBUG, L"send packet size:%d sendRingBuffer useSize:%d", 
		retVal, sessionInfo->sendRingBuffer->GetUseSize());

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
			return;
		}

		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"Header Type:%s RingBuf useSize:%d", 
			mHeaderNameData[header.msgType].c_str(),
			sessionInfo->recvRingBuffer->GetUseSize());

		sessionInfo->recvRingBuffer->MoveReadPos(returnVal);

		// 패킷버퍼에 payload 입력
		mPacketBuffer->Clear();
		returnVal = sessionInfo->recvRingBuffer->Peek(mPacketBuffer->GetBufferPtr(), header.payLoadSize);

		if (returnVal != header.payLoadSize)
		{
			CONSOLE_LOG(LOG_LEVEL_ERROR, L"Peek pay LoadSize  size Error [returnVal:%d][payloadSize:%d]", 
				returnVal, header.payLoadSize);
			return;
		}
		// 패킷 버퍼도 버퍼에 직접담은 부분이기 때문에 writepos을 직접 이동시켜준다.
		mPacketBuffer->MoveWritePos(returnVal);

		CONSOLE_LOG(LOG_LEVEL_DEBUG, L"payLoadSize:%d RingBuf useSize:%d", 
			returnVal, sessionInfo->recvRingBuffer->GetUseSize());

		sessionInfo->recvRingBuffer->MoveReadPos(returnVal);

		PacketProcess(header.msgType, sessionInfo);
	}
}

void Socket::PacketProcess(const WORD msgType, SessionInfo* clientInfo)
{
	switch (msgType)
	{
	case HEADER_CS_MOVE_START:
		{
			
		}
		break;
	case HEADER_CS_MOVE_STOP:
		{
			
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
}

void Socket::RemoveSessionInfo(SessionInfo* sessionInfo)
{
	closesocket(sessionInfo->clientSock);
	SafeDelete(sessionInfo->recvRingBuffer);
	SafeDelete(sessionInfo->sendRingBuffer);
	SafeDelete(sessionInfo);
	--mSession_IDNum;

	// 유저 데이터 삭제
	auto sessionData = mSessionData.find(sessionInfo->sessionID);
	if (sessionData == mSessionData.end())
	{
		CONSOLE_LOG(LOG_LEVEL_ERROR, L"RemoveClientInfo userData find error!");
		return;
	}
	mSessionData.erase(sessionData);
}

void Socket::HeaderNameInsert()
{
	mHeaderNameData.emplace(10, L"HEADER_CS_MOVE_START");
	mHeaderNameData.emplace(12, L"HEADER_CS_MOVE_STOP");
	mHeaderNameData.emplace(20, L"HEADER_CS_ATTACK1");
	mHeaderNameData.emplace(22, L"HEADER_CS_ATTACK2");
	mHeaderNameData.emplace(24, L"HEADER_CS_ATTACK3");
}
