#include "stdafx.h"
#include "InputOutputStream.h"
#include "SocketAddress.h"
#include "Socket.h"
#include "ThreadName.h"
#include "StringHelpers.h"
#include "..\Minecraft.Client\ServerConnection.h"
#include <algorithm>
#include "..\Minecraft.Client\PS3\PS3Extras\ShutdownManager.h"

#if defined(_WINDOWS64)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#elif defined(__PS3__)
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

typedef int SOCKET;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif
#ifndef SOMAXCONN
#define SOMAXCONN 8
#endif
#ifndef EWOULDBLOCK
	#ifdef EAGAIN
		#define EWOULDBLOCK EAGAIN
	#else
		#define EWOULDBLOCK 11
	#endif
#endif
#ifndef WSAEINTR
#define WSAEINTR EINTR
#endif
#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif

static int closesocket(int socketHandle)
{
	return close(socketHandle);
}

static int WSAGetLastError()
{
	return errno;
}
#endif

namespace
{
const unsigned long long INVALID_NATIVE_SOCKET_HANDLE = ~0ULL;

#if defined(_WINDOWS64) || defined(__PS3__)
bool ResolveIPv4Address(const char *host, in_addr *address)
{
	if(host == NULL || host[0] == '\0' || address == NULL)
	{
		return false;
	}

	#if defined(_WINDOWS64)
	if(inet_pton(AF_INET, host, address) == 1)
	{
		return true;
	}
	#else
	unsigned long parsed = inet_addr(host);
	if(parsed != INADDR_NONE || strcmp(host, "255.255.255.255") == 0)
	{
		address->s_addr = parsed;
		return true;
	}
	#endif

	hostent *hostEntry = gethostbyname(host);
	if(hostEntry != NULL && hostEntry->h_addrtype == AF_INET &&
	   hostEntry->h_addr_list != NULL && hostEntry->h_addr_list[0] != NULL)
	{
		memcpy(address, hostEntry->h_addr_list[0], sizeof(in_addr));
		return true;
	}

	return false;
}

class SocketDirectNetworkPlayer : public INetworkPlayer
{
public:
	SocketDirectNetworkPlayer(BYTE smallId, bool isHost, bool isLocal, int userIndex, const wchar_t *onlineName)
		: m_smallId(smallId)
		, m_isHost(isHost)
		, m_isLocal(isLocal)
		, m_userIndex(userIndex)
		, m_uid()
		, m_socket(NULL)
	{
	#if defined(__PS3__)
		m_uid.setUserID(0x70000000u | (unsigned int)smallId | (isHost ? 0x100u : 0u));
	#else
		m_uid = (PlayerUID)(0xF000D45200000000ULL | ((ULONGLONG)smallId << 8) | (isHost ? 1ULL : 0ULL));
	#endif

		if(onlineName != NULL)
		{
			m_onlineName = onlineName;
		}
		else
		{
			m_onlineName = L"tcp";
		}
	}

	virtual unsigned char GetSmallId() { return m_smallId; }
	virtual void SendData(INetworkPlayer *player, const void *pvData, int dataSize, bool lowPriority)
	{
		(void)(player);
		(void)(pvData);
		(void)(dataSize);
		(void)(lowPriority);
	}
	virtual bool IsSameSystem(INetworkPlayer *player)
	{
		if(player == NULL)
		{
			return false;
		}
		return player == this;
	}
	virtual int GetSendQueueSizeBytes( INetworkPlayer *player, bool lowPriority )
	{
		(void)(player);
		(void)(lowPriority);
		return 0;
	}
	virtual int GetSendQueueSizeMessages( INetworkPlayer *player, bool lowPriority )
	{
		(void)(player);
		(void)(lowPriority);
		return 0;
	}
	virtual int GetCurrentRtt() { return 0; }
	virtual bool IsHost() { return m_isHost; }
	virtual bool IsGuest() { return false; }
	virtual bool IsLocal() { return m_isLocal; }
	virtual int GetSessionIndex() { return m_smallId; }
	virtual bool IsTalking() { return false; }
	virtual bool IsMutedByLocalUser(int userIndex)
	{
		(void)(userIndex);
		return false;
	}
	virtual bool HasVoice() { return false; }
	virtual bool HasCamera() { return false; }
	virtual int GetUserIndex() { return m_userIndex; }
	virtual void SetSocket(Socket *pSocket) { m_socket = pSocket; }
	virtual Socket *GetSocket() { return m_socket; }
	virtual const wchar_t *GetOnlineName() { return m_onlineName.c_str(); }
	virtual wstring GetDisplayName() { return m_onlineName; }
	virtual PlayerUID GetUID() { return m_uid; }
	void SetOnlineName(const wstring& onlineName)
	{
		if(!onlineName.empty())
		{
			m_onlineName = onlineName;
		}
	}

private:
	BYTE m_smallId;
	bool m_isHost;
	bool m_isLocal;
	int m_userIndex;
	PlayerUID m_uid;
	Socket *m_socket;
	wstring m_onlineName;
};
#endif
}

// This current socket implementation is for the creation of a single local link. 2 sockets can be created, one for either end of this local
// link, the end (0 or 1) is passed as a parameter to the ctor.

CRITICAL_SECTION Socket::s_hostQueueLock[2];
std::queue<byte> Socket::s_hostQueue[2];
Socket::SocketOutputStreamLocal *Socket::s_hostOutStream[2];
Socket::SocketInputStreamLocal *Socket::s_hostInStream[2];
ServerConnection *Socket::s_serverConnection = NULL;
C4JThread *Socket::s_tcpAcceptThread = NULL;
volatile int Socket::s_tcpAcceptRunning = 0;
unsigned long long Socket::s_tcpListenSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
unsigned char Socket::s_nextTcpSmallId = 1;
bool Socket::s_tcpSubsystemInitialised = false;
CRITICAL_SECTION Socket::s_directPlayersLock;
bool Socket::s_directPlayersLockInitialised = false;
std::vector<INetworkPlayer *> Socket::s_directPlayers;
wstring Socket::s_lastDirectConnectHost = L"";
int Socket::s_lastDirectConnectPort = 0;

bool Socket::EnsureTcpSubsystem()
{
#if defined(_WINDOWS64)
	if(s_tcpSubsystemInitialised)
	{
		return true;
	}

	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if(result != 0)
	{
		app.DebugPrintf("WSAStartup failed with error %d\n", result);
		return false;
	}

	s_tcpSubsystemInitialised = true;
	return true;
#elif defined(__PS3__)
	s_tcpSubsystemInitialised = true;
	return true;
#else
	return false;
#endif
}

void Socket::Initialise(ServerConnection *serverConnection)
{
	s_serverConnection = serverConnection;

	if(!s_directPlayersLockInitialised)
	{
		InitializeCriticalSection(&s_directPlayersLock);
		s_directPlayersLockInitialised = true;
	}

	if(serverConnection == NULL)
	{
		StopTcpServerListener();
	}

	// Only initialise everything else once - just setting up static data, one time xrnm things, thread for ticking sockets
	static bool init = false;
	if( init )
	{
		for( int i = 0; i < 2; i++ )
		{
			if(TryEnterCriticalSection(&s_hostQueueLock[i]))
			{
				// Clear the queue
				std::queue<byte> empty;
				std::swap( s_hostQueue[i], empty );
				LeaveCriticalSection(&s_hostQueueLock[i]);
			}
			s_hostOutStream[i]->m_streamOpen = true;
			s_hostInStream[i]->m_streamOpen = true;
		}
		return;
		}
		init = true;

	for( int i = 0; i < 2; i++ )
	{
		InitializeCriticalSection(&Socket::s_hostQueueLock[i]);
		s_hostOutStream[i] = new SocketOutputStreamLocal(i);
		s_hostInStream[i] = new SocketInputStreamLocal(i);
	}
}

void Socket::RegisterDirectPlayer(INetworkPlayer *player)
{
	if(player == NULL)
	{
		return;
	}
	if(!s_directPlayersLockInitialised)
	{
		InitializeCriticalSection(&s_directPlayersLock);
		s_directPlayersLockInitialised = true;
	}

	EnterCriticalSection(&s_directPlayersLock);
	for(std::vector<INetworkPlayer *>::iterator it = s_directPlayers.begin(); it != s_directPlayers.end(); ++it)
	{
		if(*it == player)
		{
			LeaveCriticalSection(&s_directPlayersLock);
			return;
		}
	}
	s_directPlayers.push_back(player);
	LeaveCriticalSection(&s_directPlayersLock);
}

void Socket::UnregisterDirectPlayer(INetworkPlayer *player)
{
	if(player == NULL || !s_directPlayersLockInitialised)
	{
		return;
	}

	EnterCriticalSection(&s_directPlayersLock);
	for(std::vector<INetworkPlayer *>::iterator it = s_directPlayers.begin(); it != s_directPlayers.end(); ++it)
	{
		if(*it == player)
		{
			s_directPlayers.erase(it);
			break;
		}
	}
	LeaveCriticalSection(&s_directPlayersLock);
}

int Socket::GetDirectPlayerCount()
{
	if(!s_directPlayersLockInitialised)
	{
		return 0;
	}

	EnterCriticalSection(&s_directPlayersLock);
	const int count = (int)s_directPlayers.size();
	LeaveCriticalSection(&s_directPlayersLock);
	return count;
}

INetworkPlayer *Socket::GetDirectPlayerByIndex(int index)
{
	if(!s_directPlayersLockInitialised || index < 0)
	{
		return NULL;
	}

	EnterCriticalSection(&s_directPlayersLock);
	INetworkPlayer *player = (index < (int)s_directPlayers.size()) ? s_directPlayers[index] : NULL;
	LeaveCriticalSection(&s_directPlayersLock);
	return player;
}

INetworkPlayer *Socket::GetDirectPlayerBySmallId(unsigned char smallId)
{
	if(!s_directPlayersLockInitialised)
	{
		return NULL;
	}

	EnterCriticalSection(&s_directPlayersLock);
	INetworkPlayer *player = NULL;
	for(std::vector<INetworkPlayer *>::iterator it = s_directPlayers.begin(); it != s_directPlayers.end(); ++it)
	{
		if((*it) != NULL && (*it)->GetSmallId() == smallId)
		{
			player = *it;
			break;
		}
	}
	LeaveCriticalSection(&s_directPlayersLock);
	return player;
}

void Socket::StartTcpServerListener(const wstring& bindAddress, int port)
{
#if defined(_WINDOWS64) || defined(__PS3__)
	StopTcpServerListener();

	if(!EnsureTcpSubsystem())
	{
		return;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(listenSocket == INVALID_SOCKET)
	{
		app.DebugPrintf("Failed creating TCP listen socket: %d\n", WSAGetLastError());
		return;
	}

	BOOL reuseAddress = TRUE;
	setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseAddress, sizeof(reuseAddress));

	sockaddr_in address;
	ZeroMemory(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((unsigned short)port);

	bool bindOk = false;
	if(bindAddress.empty())
	{
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		bindOk = true;
	}
	else
	{
		const char *bindText = wstringtofilename(bindAddress);
		if(bindText != NULL && bindText[0] != '\0')
		{
			if(ResolveIPv4Address(bindText, &address.sin_addr))
			{
				bindOk = true;
			}
		}
	}

	if(!bindOk)
	{
		app.DebugPrintf("Unable to resolve bind address for TCP listener\n");
		closesocket(listenSocket);
		return;
	}

	if(::bind(listenSocket, (sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
	{
		app.DebugPrintf("TCP bind failed: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		return;
	}

	if(listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		app.DebugPrintf("TCP listen failed: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		return;
	}

#if defined(__PS3__)
	int nonBlocking = 1;
	setsockopt(listenSocket, SOL_SOCKET, SO_NBIO, &nonBlocking, sizeof(nonBlocking));
#endif

	s_tcpListenSocketHandle = (unsigned long long)listenSocket;
	s_tcpAcceptRunning = 1;
	s_tcpAcceptThread = new C4JThread(&Socket::DirectSocketAcceptThreadProc, NULL, "TcpSocketAccept");
	s_tcpAcceptThread->SetProcessor(CPU_CORE_CONNECTIONS);
	s_tcpAcceptThread->Run();

	app.DebugPrintf("TCP listener started on %ls:%d\n", bindAddress.empty() ? L"*" : bindAddress.c_str(), port);
#else
	(void)(bindAddress);
	(void)(port);
#endif
}

void Socket::StopTcpServerListener()
{
#if defined(_WINDOWS64) || defined(__PS3__)
	s_tcpAcceptRunning = 0;

	SOCKET listenSocket = (SOCKET)s_tcpListenSocketHandle;
	if(listenSocket != INVALID_SOCKET)
	{
		shutdown(listenSocket, SD_BOTH);
		closesocket(listenSocket);
		s_tcpListenSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	}

	if(s_tcpAcceptThread != NULL)
	{
		s_tcpAcceptThread->WaitForCompletion(INFINITE);
		delete s_tcpAcceptThread;
		s_tcpAcceptThread = NULL;
	}

#endif
}

bool Socket::GetLastDirectConnectEndpoint(wstring *host, int *port)
{
	if(s_lastDirectConnectHost.empty() || s_lastDirectConnectPort <= 0)
	{
		return false;
	}

	if(host != NULL)
	{
		*host = s_lastDirectConnectHost;
	}
	if(port != NULL)
	{
		*port = s_lastDirectConnectPort;
	}
	return true;
}

void Socket::initialiseNetworkQueues(bool response, bool hostLocal)
{
	m_hostServerConnection = false;
	m_hostLocal = hostLocal;

	for( int i = 0; i < 2; i++ )
	{
		InitializeCriticalSection(&m_queueLockNetwork[i]);
		m_inputStream[i] = NULL;
		m_outputStream[i] = NULL;
		m_endClosed[i] = false;
	}

	if(!response || hostLocal)
	{
		m_inputStream[SOCKET_CLIENT_END] = new SocketInputStreamNetwork(this, SOCKET_CLIENT_END);
		m_outputStream[SOCKET_CLIENT_END] = new SocketOutputStreamNetwork(this, SOCKET_CLIENT_END);
		m_end = SOCKET_CLIENT_END;
	}
	if(response || hostLocal)
	{
		m_inputStream[SOCKET_SERVER_END] = new SocketInputStreamNetwork(this, SOCKET_SERVER_END);
		m_outputStream[SOCKET_SERVER_END] = new SocketOutputStreamNetwork(this, SOCKET_SERVER_END);
		m_end = SOCKET_SERVER_END;
	}

	m_socketClosedEvent = new C4JThread::Event;
}

Socket::Socket(bool response)
{
	m_useTcpTransport = false;
	m_tcpAcceptedServerSide = false;
	m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	m_tcpReadThread = NULL;
	m_tcpReadRunning = 0;
	m_directNetworkPlayer = NULL;

	m_hostServerConnection = true;
	m_hostLocal = true;
	if( response )
	{
		m_end = SOCKET_SERVER_END;
	}
	else
	{
		m_end = SOCKET_CLIENT_END;
		Socket *socket = new Socket(true);
		s_serverConnection->NewIncomingSocket(socket);
	}

	for( int i = 0; i < 2; i++ )
	{
		m_endClosed[i] = false;
	}
	m_socketClosedEvent = NULL;
	createdOk = true;
	INetworkPlayer *hostPlayer = g_NetworkManager.GetHostPlayer();
	networkPlayerSmallId = hostPlayer != NULL ? hostPlayer->GetSmallId() : 0;
}

Socket::Socket(INetworkPlayer *player, bool response /* = false*/, bool hostLocal /*= false*/)
{
	m_useTcpTransport = false;
	m_tcpAcceptedServerSide = false;
	m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	m_tcpReadThread = NULL;
	m_tcpReadRunning = 0;
	m_directNetworkPlayer = NULL;

	initialiseNetworkQueues(response, hostLocal);

	//printf("New socket made %s\n", player->GetGamertag() );
	networkPlayerSmallId = player != NULL ? player->GetSmallId() : 0;
	createdOk = true;
}

Socket::Socket(const wstring& ip, int port)
{
	m_useTcpTransport = false;
	m_tcpAcceptedServerSide = false;
	m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	m_tcpReadThread = NULL;
	m_tcpReadRunning = 0;
	m_directNetworkPlayer = NULL;

	initialiseNetworkQueues(false, false);

	networkPlayerSmallId = 0;
#if defined(_WINDOWS64) || defined(__PS3__)
	// Client-side direct socket represents the remote host player.
	m_directNetworkPlayer = new SocketDirectNetworkPlayer(networkPlayerSmallId, true, false, -1, L"host");
	m_directNetworkPlayer->SetSocket(this);
	RegisterDirectPlayer(m_directNetworkPlayer);
#endif

	createdOk = connectDirectSocket(ip, port);
	if(!createdOk)
	{
		closeDirectSocket();
	}
}

Socket::Socket(unsigned long long connectedSocketHandle, bool serverSide)
{
	m_useTcpTransport = false;
	m_tcpAcceptedServerSide = serverSide;
	m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	m_tcpReadThread = NULL;
	m_tcpReadRunning = 0;
	m_directNetworkPlayer = NULL;

	initialiseNetworkQueues(serverSide, false);

	if(s_nextTcpSmallId == 0)
	{
		s_nextTcpSmallId = 1;
	}
	networkPlayerSmallId = s_nextTcpSmallId++;
#if defined(_WINDOWS64) || defined(__PS3__)
	m_directNetworkPlayer = new SocketDirectNetworkPlayer(networkPlayerSmallId, false, false, -1, L"remote");
	m_directNetworkPlayer->SetSocket(this);
	RegisterDirectPlayer(m_directNetworkPlayer);
#endif

	createdOk = initialiseDirectSocket(connectedSocketHandle, serverSide);
	if(!createdOk)
	{
		closeDirectSocket();
	}
}

bool Socket::initialiseDirectSocket(unsigned long long connectedSocketHandle, bool serverSide)
{
	(void)(serverSide);
#if defined(_WINDOWS64) || defined(__PS3__)
	if(!EnsureTcpSubsystem())
	{
		return false;
	}

	SOCKET nativeSocket = (SOCKET)connectedSocketHandle;
	if(nativeSocket == INVALID_SOCKET)
	{
		return false;
	}

	BOOL noDelay = TRUE;
	setsockopt(nativeSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof(noDelay));

	m_useTcpTransport = true;
	m_tcpSocketHandle = connectedSocketHandle;
	m_tcpReadRunning = 1;

	m_tcpReadThread = new C4JThread(&Socket::DirectSocketReadThreadProc, this, serverSide ? "TcpReadServer" : "TcpReadClient");
	m_tcpReadThread->SetProcessor(CPU_CORE_CONNECTIONS);
	m_tcpReadThread->Run();
	app.DebugPrintf("Direct TCP socket initialised (%s side), smallId=%d\n", serverSide ? "server" : "client", (int)networkPlayerSmallId);

	return true;
#else
	(void)(connectedSocketHandle);
	return false;
#endif
}

bool Socket::connectDirectSocket(const wstring& ip, int port)
{
#if defined(_WINDOWS64) || defined(__PS3__)
	if(!EnsureTcpSubsystem())
	{
		return false;
	}

	SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(socketHandle == INVALID_SOCKET)
	{
		app.DebugPrintf("Failed creating TCP client socket: %d\n", WSAGetLastError());
		return false;
	}

	const char *hostText = ip.empty() ? "127.0.0.1" : wstringtofilename(ip);
	string host = (hostText != NULL && hostText[0] != '\0') ? string(hostText) : string("127.0.0.1");

	sockaddr_in address;
	ZeroMemory(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((unsigned short)port);

	bool hasAddress = false;
	if(ResolveIPv4Address(host.c_str(), &address.sin_addr))
	{
		hasAddress = true;
	}

	if(!hasAddress)
	{
		app.DebugPrintf("Unable to resolve host: %s\n", host.c_str());
		closesocket(socketHandle);
		return false;
	}

	if(connect(socketHandle, (sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
	{
		app.DebugPrintf("TCP connect failed to %s:%d with error %d\n", host.c_str(), port, WSAGetLastError());
		closesocket(socketHandle);
		return false;
	}

	app.DebugPrintf("Connected to TCP server %s:%d\n", host.c_str(), port);
	const bool initialiseOk = initialiseDirectSocket((unsigned long long)socketHandle, false);
	if(initialiseOk)
	{
		s_lastDirectConnectHost = convStringToWstring(host);
		s_lastDirectConnectPort = port;
	}
	return initialiseOk;
#else
	(void)(ip);
	(void)(port);
	return false;
#endif
}

int Socket::DirectSocketReadThreadProc(void *lpParam)
{
	Socket *socket = (Socket *)lpParam;
	if(socket == NULL)
	{
		return 0;
	}

#if defined(_WINDOWS64) || defined(__PS3__)
	SOCKET nativeSocket = (SOCKET)socket->m_tcpSocketHandle;
	if(nativeSocket == INVALID_SOCKET)
	{
		app.DebugPrintf("Direct TCP read thread started with invalid socket handle\n");
		socket->m_tcpReadRunning = 0;
		return 0;
	}

	BYTE readBuffer[4096];
	bool loggedFirstRead = false;
	while(socket->m_tcpReadRunning)
	{
		int bytesRead = recv(nativeSocket, (char *)readBuffer, sizeof(readBuffer), 0);
		if(bytesRead > 0)
		{
			if(!loggedFirstRead)
			{
				app.DebugPrintf("Direct TCP read received %d bytes (first read)\n", bytesRead);
				loggedFirstRead = true;
			}
			socket->enqueueReceivedData(readBuffer, (unsigned int)bytesRead);
			continue;
		}

		if(bytesRead == 0)
		{
			app.DebugPrintf("Direct TCP read got EOF from peer\n");
			break;
		}

		const int error = WSAGetLastError();
		if(error == WSAEINTR)
		{
			continue;
		}
		if(error == WSAEWOULDBLOCK)
		{
			Sleep(1);
			continue;
		}

		app.DebugPrintf("TCP recv failed with error %d\n", error);
		break;
	}
	app.DebugPrintf("Direct TCP read thread exiting\n");

	socket->m_tcpReadRunning = 0;

	SOCKET socketToClose = (SOCKET)socket->m_tcpSocketHandle;
	if(socketToClose != INVALID_SOCKET)
	{
		shutdown(socketToClose, SD_BOTH);
		closesocket(socketToClose);
		socket->m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	}

#endif

	if(socket->m_inputStream[socket->m_end] != NULL)
	{
		socket->m_inputStream[socket->m_end]->close();
	}
	socket->m_endClosed[socket->m_end] = true;
	socket->createdOk = false;
	if(socket->m_socketClosedEvent != NULL)
	{
		socket->m_socketClosedEvent->Set();
	}

	return 0;
}

int Socket::DirectSocketAcceptThreadProc(void *lpParam)
{
	(void)(lpParam);
#if defined(_WINDOWS64) || defined(__PS3__)
	while(s_tcpAcceptRunning)
	{
		SOCKET listenSocket = (SOCKET)s_tcpListenSocketHandle;
		if(listenSocket == INVALID_SOCKET)
		{
			break;
		}

#if defined(__PS3__)
		SOCKET acceptedSocket = accept(listenSocket, NULL, NULL);
		if(acceptedSocket == INVALID_SOCKET)
		{
			const int error = WSAGetLastError();
			if(error == WSAEINTR || error == WSAEWOULDBLOCK)
			{
				Sleep(10);
				continue;
			}

			if(!s_tcpAcceptRunning)
			{
				break;
			}

			Sleep(10);
			continue;
		}
#else
		fd_set socketsToRead;
		FD_ZERO(&socketsToRead);
		FD_SET(listenSocket, &socketsToRead);

		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 200000;

		int result = 0;
#if defined(__PS3__)
		result = select(listenSocket + 1, &socketsToRead, NULL, NULL, &timeout);
#else
		result = select(0, &socketsToRead, NULL, NULL, &timeout);
#endif
		if(result <= 0)
		{
			continue;
		}

		SOCKET acceptedSocket = accept(listenSocket, NULL, NULL);
		if(acceptedSocket == INVALID_SOCKET)
		{
			continue;
		}
#endif

		Socket *socket = new Socket((unsigned long long)acceptedSocket, true);
		if(socket == NULL)
		{
			closesocket(acceptedSocket);
			continue;
		}
		if(!socket->createdOk)
		{
			delete socket;
			continue;
		}

		Socket::addIncomingSocket(socket);
		app.DebugPrintf("Accepted TCP client connection\n");
	}
#endif
	return 0;
}

void Socket::closeDirectSocket()
{
#if defined(_WINDOWS64) || defined(__PS3__)
	m_tcpReadRunning = 0;

	SOCKET nativeSocket = (SOCKET)m_tcpSocketHandle;
	if(nativeSocket != INVALID_SOCKET)
	{
		shutdown(nativeSocket, SD_BOTH);
		closesocket(nativeSocket);
		m_tcpSocketHandle = INVALID_NATIVE_SOCKET_HANDLE;
	}
#endif

	if(m_tcpReadThread != NULL)
	{
		if(C4JThread::getCurrentThread() != m_tcpReadThread)
		{
			m_tcpReadThread->WaitForCompletion(INFINITE);
		}
		delete m_tcpReadThread;
		m_tcpReadThread = NULL;
	}

	if(m_directNetworkPlayer != NULL)
	{
		UnregisterDirectPlayer(m_directNetworkPlayer);
		delete m_directNetworkPlayer;
		m_directNetworkPlayer = NULL;
	}

}

bool Socket::sendDirectData(const BYTE *pbData, unsigned int dataSize)
{
#if defined(_WINDOWS64) || defined(__PS3__)
	if(!m_useTcpTransport)
	{
		return false;
	}

	SOCKET nativeSocket = (SOCKET)m_tcpSocketHandle;
	if(nativeSocket == INVALID_SOCKET)
	{
		return false;
	}

	unsigned int sentBytes = 0;
	while(sentBytes < dataSize)
	{
		int sent = send(nativeSocket, (const char *)pbData + sentBytes, (int)(dataSize - sentBytes), 0);
		if(sent == 0)
		{
			app.DebugPrintf("TCP send returned 0 (peer disconnected)\n");
			m_endClosed[m_end] = true;
			createdOk = false;
			if(m_socketClosedEvent != NULL)
			{
				m_socketClosedEvent->Set();
			}
			return false;
		}
		if(sent == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if(error == WSAEINTR)
			{
				continue;
			}
			if(error == WSAEWOULDBLOCK)
			{
				Sleep(1);
				continue;
			}

			app.DebugPrintf("TCP send failed with error %d\n", error);
			m_endClosed[m_end] = true;
			createdOk = false;
			if(m_socketClosedEvent != NULL)
			{
				m_socketClosedEvent->Set();
			}
			return false;
		}
		sentBytes += sent;
	}
	return true;
#else
	(void)(pbData);
	(void)(dataSize);
	return false;
#endif
}

void Socket::enqueueReceivedData(const BYTE *pbData, unsigned int dataSize)
{
	if(pbData == NULL || dataSize == 0)
	{
		return;
	}

	EnterCriticalSection(&m_queueLockNetwork[m_end]);
	for(unsigned int i = 0; i < dataSize; ++i)
	{
		m_queueNetwork[m_end].push(pbData[i]);
	}
	LeaveCriticalSection(&m_queueLockNetwork[m_end]);
}

SocketAddress *Socket::getRemoteSocketAddress()
{
	return NULL;
}

INetworkPlayer *Socket::getPlayer()
{
	if(m_directNetworkPlayer != NULL)
	{
		if(!m_tcpAcceptedServerSide && m_directNetworkPlayer->IsHost() && networkPlayerSmallId != 0)
		{
			INetworkPlayer *mappedPlayer = g_NetworkManager.GetPlayerBySmallId(networkPlayerSmallId);
			if(mappedPlayer != NULL)
			{
				return mappedPlayer;
			}
		}

		return m_directNetworkPlayer;
	}
	return g_NetworkManager.GetPlayerBySmallId(networkPlayerSmallId);
}

void Socket::setPlayer(INetworkPlayer *player)
{
	if(player != NULL)
	{
		networkPlayerSmallId = player->GetSmallId();
	}
	else
	{
		networkPlayerSmallId = 0;
	}
}

void Socket::setDirectPlayerOnlineName(const wstring& onlineName)
{
#if defined(_WINDOWS64) || defined(__PS3__)
	if(onlineName.empty() || m_directNetworkPlayer == NULL)
	{
		return;
	}

	SocketDirectNetworkPlayer *directPlayer = (SocketDirectNetworkPlayer *)m_directNetworkPlayer;
	directPlayer->SetOnlineName(onlineName);
#else
	(void)(onlineName);
#endif
}

void Socket::pushDataToQueue(const BYTE * pbData, DWORD dwDataSize, bool fromHost /*= true*/)
{
	int queueIdx = SOCKET_CLIENT_END;
	if(!fromHost)
		queueIdx = SOCKET_SERVER_END;

	if( queueIdx != m_end && !m_hostLocal )
	{
		app.DebugPrintf("SOCKET: Error pushing data to queue. End is %d but queue idx id %d\n", m_end, queueIdx);
		return;
	}

	EnterCriticalSection(&m_queueLockNetwork[queueIdx]);
	for( unsigned int i = 0; i < dwDataSize; i++ )
	{
		m_queueNetwork[queueIdx].push(*pbData++);
	}
	LeaveCriticalSection(&m_queueLockNetwork[queueIdx]);
}

void Socket::addIncomingSocket(Socket *socket)
{
	if( s_serverConnection != NULL )
	{
		s_serverConnection->NewIncomingSocket(socket);
	}
}

InputStream *Socket::getInputStream(bool isServerConnection)
{
	if( !m_hostServerConnection )
	{
		if( m_hostLocal )
		{
			if( isServerConnection )
			{
				return m_inputStream[SOCKET_SERVER_END];
			}
			else
			{
				return m_inputStream[SOCKET_CLIENT_END];
			}
		}
		else
		{
			return m_inputStream[m_end];
		}
	}
	else
	{
		return s_hostInStream[m_end];
	}
}

void Socket::setSoTimeout(int a )
{
	(void)(a);
}

void Socket::setTrafficClass( int a )
{
	(void)(a);
}

Socket::SocketOutputStream *Socket::getOutputStream(bool isServerConnection)
{
	if( !m_hostServerConnection )
	{
		if( m_hostLocal )
		{
			if( isServerConnection )
			{
				return m_outputStream[SOCKET_SERVER_END];
			}
			else
			{
				return m_outputStream[SOCKET_CLIENT_END];
			}
		}
		else
		{
			return m_outputStream[m_end];
		}
	}
	else
	{
		return s_hostOutStream[ 1 - m_end ];
	}
}

bool Socket::close(bool isServerConnection)
{
	bool allClosed = false;
	if( m_hostLocal )
	{
		if( isServerConnection )
		{
			m_endClosed[SOCKET_SERVER_END] = true;
			if(m_endClosed[SOCKET_CLIENT_END])
			{
				allClosed = true;
			}
		}
		else
		{
			m_endClosed[SOCKET_CLIENT_END] = true;
			if(m_endClosed[SOCKET_SERVER_END])
			{
				allClosed = true;
			}
		}
	}
	else
	{
		allClosed = true;
		m_endClosed[m_end] = true;
	}

	if(allClosed && m_useTcpTransport)
	{
		closeDirectSocket();
	}

	if( allClosed && m_socketClosedEvent != NULL )
	{
		m_socketClosedEvent->Set();
	}
	if(allClosed) createdOk = false;
	return allClosed;
}

/////////////////////////////////// Socket for input, on local connection ////////////////////

Socket::SocketInputStreamLocal::SocketInputStreamLocal(int queueIdx)
{
	m_streamOpen = true;
	m_queueIdx = queueIdx;
}

// Try and get an input byte, blocking until one is available
int Socket::SocketInputStreamLocal::read()
{
	while(m_streamOpen && ShutdownManager::ShouldRun(ShutdownManager::eConnectionReadThreads))
	{
		if(TryEnterCriticalSection(&s_hostQueueLock[m_queueIdx]))
		{
			if( s_hostQueue[m_queueIdx].size() )
			{
				byte retval = s_hostQueue[m_queueIdx].front();
				s_hostQueue[m_queueIdx].pop();
				LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
				return retval;
			}
			LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
		}
		Sleep(1);
	}
	return -1;
}

// Try and get an input array of bytes, blocking until enough bytes are available
int Socket::SocketInputStreamLocal::read(byteArray b)
{
	return read(b, 0, b.length);
}

// Try and get an input range of bytes, blocking until enough bytes are available
int Socket::SocketInputStreamLocal::read(byteArray b, unsigned int offset, unsigned int length)
{
	while(m_streamOpen)
	{
		if(TryEnterCriticalSection(&s_hostQueueLock[m_queueIdx]))
		{
			if( s_hostQueue[m_queueIdx].size() >= length )
			{
				for( unsigned int i = 0; i < length; i++ )
				{
					b[i+offset] = s_hostQueue[m_queueIdx].front();
					s_hostQueue[m_queueIdx].pop();
				}
				LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
				return length;
			}
			LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
		}
		Sleep(1);
	}
	return -1;
}

void Socket::SocketInputStreamLocal::close()
{
	m_streamOpen = false;
	EnterCriticalSection(&s_hostQueueLock[m_queueIdx]);
	s_hostQueue[m_queueIdx].empty();
	LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
}

/////////////////////////////////// Socket for output, on local connection ////////////////////

Socket::SocketOutputStreamLocal::SocketOutputStreamLocal(int queueIdx)
{
	m_streamOpen = true;
	m_queueIdx = queueIdx;
}

void Socket::SocketOutputStreamLocal::write(unsigned int b)
{
	if( m_streamOpen != true )
	{
		return;
	}
	EnterCriticalSection(&s_hostQueueLock[m_queueIdx]);
	s_hostQueue[m_queueIdx].push((byte)b);
	LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
}

void Socket::SocketOutputStreamLocal::write(byteArray b)
{
	write(b, 0, b.length);
}

void Socket::SocketOutputStreamLocal::write(byteArray b, unsigned int offset, unsigned int length)
{
	if( m_streamOpen != true )
	{
		return;
	}
	MemSect(12);
	EnterCriticalSection(&s_hostQueueLock[m_queueIdx]);
	for( unsigned int i = 0; i < length; i++ )
	{
		s_hostQueue[m_queueIdx].push(b[offset+i]);
	}
	LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
	MemSect(0);
}

void Socket::SocketOutputStreamLocal::close()
{
	m_streamOpen = false;
	EnterCriticalSection(&s_hostQueueLock[m_queueIdx]);
	s_hostQueue[m_queueIdx].empty();
	LeaveCriticalSection(&s_hostQueueLock[m_queueIdx]);
}

/////////////////////////////////// Socket for input, on network connection ////////////////////

Socket::SocketInputStreamNetwork::SocketInputStreamNetwork(Socket *socket, int queueIdx)
{
	m_streamOpen = true;
	m_queueIdx = queueIdx;
	m_socket = socket;
}

// Try and get an input byte, blocking until one is available
int Socket::SocketInputStreamNetwork::read()
{
	while(m_streamOpen && ShutdownManager::ShouldRun(ShutdownManager::eConnectionReadThreads))
	{
		if(TryEnterCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]))
		{
			if( m_socket->m_queueNetwork[m_queueIdx].size() )
			{
				byte retval = m_socket->m_queueNetwork[m_queueIdx].front();
				m_socket->m_queueNetwork[m_queueIdx].pop();
				LeaveCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]);
				return retval;
			}
			LeaveCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]);
		}
		Sleep(1);
	}
	return -1;
}

// Try and get an input array of bytes, blocking until enough bytes are available
int Socket::SocketInputStreamNetwork::read(byteArray b)
{
	return read(b, 0, b.length);
}

// Try and get an input range of bytes, blocking until enough bytes are available
int Socket::SocketInputStreamNetwork::read(byteArray b, unsigned int offset, unsigned int length)
{
	while(m_streamOpen)
	{
		if(TryEnterCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]))
		{
			if( m_socket->m_queueNetwork[m_queueIdx].size() >= length )
			{
				for( unsigned int i = 0; i < length; i++ )
				{
					b[i+offset] = m_socket->m_queueNetwork[m_queueIdx].front();
					m_socket->m_queueNetwork[m_queueIdx].pop();
				}
				LeaveCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]);
				return length;
			}
			LeaveCriticalSection(&m_socket->m_queueLockNetwork[m_queueIdx]);
		}
		Sleep(1);
	}
	return -1;
}

bool Socket::SocketInputStreamNetwork::isDirectTcpTransport() const
{
	return m_socket != NULL && m_socket->m_useTcpTransport;
}

void Socket::SocketInputStreamNetwork::close()
{
	m_streamOpen = false;
}

/////////////////////////////////// Socket for output, on network connection ////////////////////

Socket::SocketOutputStreamNetwork::SocketOutputStreamNetwork(Socket *socket, int queueIdx)
{
	m_queueIdx = queueIdx;
	m_socket = socket;
	m_streamOpen = true;
}

void Socket::SocketOutputStreamNetwork::write(unsigned int b)
{
	if( m_streamOpen != true ) return;
	byteArray barray;
	byte bb;
	bb = (byte)b;
	barray.data = &bb;
	barray.length = 1;
	write(barray, 0, 1);

}

void Socket::SocketOutputStreamNetwork::write(byteArray b)
{
	write(b, 0, b.length);
}

void Socket::SocketOutputStreamNetwork::write(byteArray b, unsigned int offset, unsigned int length)
{
	writeWithFlags(b, offset, length, 0);
}

void Socket::SocketOutputStreamNetwork::writeWithFlags(byteArray b, unsigned int offset, unsigned int length, int flags)
{
	if( m_streamOpen != true ) return;
	if( length == 0 ) return;

	if( m_socket->m_useTcpTransport )
	{
		m_socket->sendDirectData((const BYTE *)&b[offset], length);
		return;
	}

	// If this is a local connection, don't bother going through QNet as it just delivers it straight anyway
	if( m_socket->m_hostLocal )
	{
		// We want to write to the queue for the other end of this socket stream
		int queueIdx = m_queueIdx;
		if(queueIdx == SOCKET_CLIENT_END)
			queueIdx = SOCKET_SERVER_END;
		else
			queueIdx = SOCKET_CLIENT_END;

		EnterCriticalSection(&m_socket->m_queueLockNetwork[queueIdx]);
		for( unsigned int i = 0; i < length; i++ )
		{
			m_socket->m_queueNetwork[queueIdx].push(b[offset+i]);
		}
		LeaveCriticalSection(&m_socket->m_queueLockNetwork[queueIdx]);
	}
	else
	{
		XRNM_SEND_BUFFER buffer;
		buffer.pbyData = &b[offset];
		buffer.dwDataSize = length;

		INetworkPlayer *hostPlayer = g_NetworkManager.GetHostPlayer();
		if(hostPlayer == NULL)
		{
			app.DebugPrintf("Trying to write to network, but the hostPlayer is NULL\n");
			return;
		}
		INetworkPlayer *socketPlayer = m_socket->getPlayer();
		if(socketPlayer == NULL)
		{
			app.DebugPrintf("Trying to write to network, but the socketPlayer is NULL\n");
			return;
		}

		if( m_queueIdx == SOCKET_SERVER_END )
		{
			hostPlayer->SendData(socketPlayer, buffer.pbyData, buffer.dwDataSize, QNET_SENDDATA_RELIABLE | QNET_SENDDATA_SEQUENTIAL | flags);
		}
		else
		{
			socketPlayer->SendData(hostPlayer, buffer.pbyData, buffer.dwDataSize, QNET_SENDDATA_RELIABLE | QNET_SENDDATA_SEQUENTIAL | flags);
		}
	}
}

void Socket::SocketOutputStreamNetwork::close()
{
	m_streamOpen = false;
}

bool Socket::SocketOutputStreamNetwork::isDirectTcpTransport() const
{
	return m_socket != NULL && m_socket->m_useTcpTransport;
}
