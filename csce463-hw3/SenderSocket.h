#pragma once


#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include "PacketHeaders.h"
#include <chrono>
#include <mutex>
#include <thread>

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

// todo: for stats sleep on event quit with two second timeout

class Packet {
public:
	// int type; // SYN, FIN, data
	int size; // bytes in packet data
	clock_t txTime; // transmission time
	char pkt[MAX_PKT_SIZE]; // packet with header
};
class SenderSocket
{
private:
	SOCKET sock;
	sockaddr_in local;
	sockaddr_in remote;
	std::chrono::steady_clock::time_point constructedTime;
	std::thread worker;
	std::thread stats;
	double RTO;
	double estRTT;
	double devRTT;
	double timerExpire;
	boolean recomputeTimerExpire;
	int baseRetxCount = 0;
	int window;
	DWORD senderBase = 0;
	int produced = 0;
	int seqNum = 0;
	int nextToSend = 0;
	int maxAttempsSYN = 3;
	int maxAttempsFIN = 5;
	// semaphores
	HANDLE empty;
	HANDLE full;
	HANDLE socketReceiveReady;
	HANDLE eventQuit;

	// buffer
	Packet* buffer;
	std::mutex mtx;

	// stats variables
	double mb = 0.0;
	uint64_t totalAckedBytes = 0;
	double lastStatsTime = 0.0;
	DWORD lastStatsBase = 0;
	int timeoutCount = 0;
	int fastRetx = 0;
	DWORD receiverWindow = 0;
	double goodput = 0.0;
	void closeSocket();
	double getElapsedTime();
	void updateRTO(double RTT);
	void sendPacket(const char *buf, const int &bytes);
	void WorkerRun();
	void recvPacket();
	void StatsRun();

public:
	SenderSocket();
	~SenderSocket();
	int Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties);
	int Send(char *buf, int bytes);
	int Close(double &elapsedTime);
	double getEstRTT();
};
