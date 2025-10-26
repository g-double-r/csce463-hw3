// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
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
	#define NOMINMAX
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	#pragma comment(lib, "Ws2_32.lib")
#endif

#include "SenderSocket.h"
#include "PacketHeaders.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <algorithm>

#endif //PCH_H
