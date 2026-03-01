#pragma once
#include <xrnm.h>
#include <queue>
#include <vector>
#include <qnet.h>
#include "InputStream.h"
#include "OutputStream.h"

#define SOCKET_CLIENT_END 0
#define SOCKET_SERVER_END 1

class SocketAddress;
class ServerConnection;

class Socket
{
public:
	// 4J Added so we can add a priority write function
	class SocketOutputStream : public OutputStream
	{
	public:
		// The flags are those that can be used for the QNet SendData function
		virtual void writeWithFlags(byteArray b, unsigned int offset, unsigned int length, int flags) { write(b, offset, length); }
	};

private:
	class SocketInputStreamLocal : public InputStream
	{
	public:
		bool m_streamOpen;
	private:
		int	m_queueIdx;
	public:
		SocketInputStreamLocal(int queueIdx);

		virtual int read();
		virtual int read(byteArray b);
		virtual int read(byteArray b, unsigned int offset, unsigned int length);
		virtual void close();
		virtual __int64 skip(__int64 n) { return n; } // 4J Stu - Not implemented
		virtual void flush() {}
	};

	class SocketOutputStreamLocal : public SocketOutputStream
	{
	public:
		bool m_streamOpen;
	private:
		int m_queueIdx;
	public:
		SocketOutputStreamLocal(int queueIdx);

		virtual void write(unsigned int b);
		virtual void write(byteArray b);
		virtual void write(byteArray b, unsigned int offset, unsigned int length);
		virtual void close();
		virtual void flush() {}
	};

	class SocketInputStreamNetwork : public InputStream
	{
		bool m_streamOpen;
		int	m_queueIdx;
		Socket *m_socket;
		public:
			SocketInputStreamNetwork(Socket *socket, int queueIdx);

			virtual int read();
			virtual int read(byteArray b);
			virtual int read(byteArray b, unsigned int offset, unsigned int length);
			virtual void close();
			virtual __int64 skip(__int64 n) { return n; } // 4J Stu - Not implemented
			virtual void flush() {}
			virtual bool isDirectTcpTransport() const;
		};
	class SocketOutputStreamNetwork : public SocketOutputStream
	{
		bool m_streamOpen;
		int	m_queueIdx;
		Socket *m_socket;
		public:
			SocketOutputStreamNetwork(Socket *socket, int queueIdx);

			virtual void write(unsigned int b);
			virtual void write(byteArray b);
			virtual void write(byteArray b, unsigned int offset, unsigned int length);
			virtual void writeWithFlags(byteArray b, unsigned int offset, unsigned int length, int flags);
			virtual void close();
			virtual void flush() {}
			virtual bool isDirectTcpTransport() const;
		};
	
	bool m_hostServerConnection;			// true if this is the connection between the host player and server
	bool m_hostLocal;						// true if this player on the same machine as the host
	int m_end;								// 0 for client side or 1 for host side
	bool m_useTcpTransport;					// true if this socket uses OS-level TCP transport
	bool m_tcpAcceptedServerSide;			// true if this socket came from the TCP listener
	unsigned long long m_tcpSocketHandle;
	C4JThread *m_tcpReadThread;
	volatile int m_tcpReadRunning;
	INetworkPlayer *m_directNetworkPlayer;

	// For local connections between the host player and the server
	static CRITICAL_SECTION s_hostQueueLock[2]; 
	static std::queue<byte> s_hostQueue[2];
	static SocketOutputStreamLocal *s_hostOutStream[2];
	static SocketInputStreamLocal *s_hostInStream[2];

	// For network connections
	std::queue<byte> m_queueNetwork[2];		// For input data
	CRITICAL_SECTION m_queueLockNetwork[2];  // For input data
	SocketInputStreamNetwork *m_inputStream[2];
	SocketOutputStreamNetwork *m_outputStream[2];
	bool m_endClosed[2];

	// Host only connection class
	static ServerConnection *s_serverConnection;
	static C4JThread *s_tcpAcceptThread;
	static volatile int s_tcpAcceptRunning;
	static unsigned long long s_tcpListenSocketHandle;
	static unsigned char s_nextTcpSmallId;
	static bool s_tcpSubsystemInitialised;
	static CRITICAL_SECTION s_directPlayersLock;
	static bool s_directPlayersLockInitialised;
	static std::vector<INetworkPlayer *> s_directPlayers;
	static wstring s_lastDirectConnectHost;
	static int s_lastDirectConnectPort;

	BYTE networkPlayerSmallId;

	static void RegisterDirectPlayer(INetworkPlayer *player);
	static void UnregisterDirectPlayer(INetworkPlayer *player);
	void initialiseNetworkQueues(bool response, bool hostLocal);
	void enqueueReceivedData(const BYTE *pbData, unsigned int dataSize);
	bool initialiseDirectSocket(unsigned long long connectedSocketHandle, bool serverSide);
	bool connectDirectSocket(const wstring& ip, int port);
	bool sendDirectData(const BYTE *pbData, unsigned int dataSize);
	void closeDirectSocket();
	static bool EnsureTcpSubsystem();
	static int DirectSocketReadThreadProc(void *lpParam);
	static int DirectSocketAcceptThreadProc(void *lpParam);
public:	
	C4JThread::Event* m_socketClosedEvent;

	INetworkPlayer *getPlayer();
	void setPlayer(INetworkPlayer *player);
	void setDirectPlayerOnlineName(const wstring& onlineName);

public:
	static void Initialise(ServerConnection *serverConnection);
	static void StartTcpServerListener(const wstring& bindAddress, int port);
	static void StopTcpServerListener();
	static bool GetLastDirectConnectEndpoint(wstring *host, int *port);
	Socket(bool response = false);								// 4J - Create a local socket, for end 0 or 1 of a connection
	Socket(const wstring& ip, int port);						// Create a direct TCP client socket
	Socket(unsigned long long connectedSocketHandle, bool serverSide); // Create a direct TCP socket from an accepted connection
	Socket(INetworkPlayer *player, bool response  = false, bool hostLocal = false);		// 4J - Create a socket for an INetworkPlayer
	SocketAddress *getRemoteSocketAddress();
	void pushDataToQueue(const BYTE * pbData, DWORD dwDataSize, bool fromHost = true);
	static void addIncomingSocket(Socket *socket);
	static int GetDirectPlayerCount();
	static INetworkPlayer *GetDirectPlayerByIndex(int index);
	static INetworkPlayer *GetDirectPlayerBySmallId(unsigned char smallId);
	InputStream *getInputStream(bool isServerConnection);
	void setSoTimeout(int a );
	void setTrafficClass( int a );
	SocketOutputStream *getOutputStream(bool isServerConnection);
	bool close(bool isServerConnection);
	bool createdOk;
	bool isLocal() { return m_hostLocal; }
	bool isTcpTransport() const { return m_useTcpTransport; }

	bool isClosing() { return m_endClosed[SOCKET_CLIENT_END] || m_endClosed[SOCKET_SERVER_END]; }
	BYTE getSmallId() { return networkPlayerSmallId; }
};
