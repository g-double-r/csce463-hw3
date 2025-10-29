#pragma once

#ifdef __APPLE__
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/select.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <unistd.h>
	#include <netdb.h>
	#define WSAGetLastError() (errno)
	#define WSACleanup() ((void)0)
	typedef int SOCKET;
	typedef uint32_t DWORD;
	#define INVALID_SOCKET (-1)
	#define SOCKET_ERROR (-1)
#else
	#define _WINSOCK_DEPRECATED_NO_WARNINGS
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	#pragma comment(lib, "Ws2_32.lib")
#endif

#include "PacketHeaders.h"
#include <chrono>

// CONSTANTS
#define MAGIC_PORT 22345		 // receiver listens on this port
#define MAX_PKT_SIZE (1500 - 28) // maximum UDP packet size accepted by receiver

// possible status codes from ss.Open, ss.Send, ss.Close
#define STATUS_OK 0			// no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2		// call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3		// ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4		// sendto() failed in kernel
#define TIMEOUT 5			// timeout after all retx attempts are exhausted
#define FAILED_RECV 6		// recvfrom() failed in kernel

class SenderSocket
{
private:
	SOCKET sock;
	sockaddr_in local;
	sockaddr_in remote;
	std::chrono::steady_clock::time_point constructedTime;
	double RTO = 1;
	int maxAttempsSYN = 3;
	int maxAttempsFIN = 5;
	// TODO: update after part1
	// short window = 1;
	void closeSocket();
	double getElapsedTime();

public:
	SenderSocket();
	~SenderSocket();
	int Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties);
	int Send(char *buf, int bytes);
	int Close();
};
