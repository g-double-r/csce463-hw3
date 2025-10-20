#include "SenderSocket.h"
#include "pch.h"

#ifdef _WIN32
void initializeWinsock()
{
    WSADATA wsaData;

    // Initialize WinSock once per program run
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0)
    {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}

void cleanUpWinsock()
{
    // call cleanup when done with everything and ready to exit program
    WSACleanup();
}
#endif

int Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties) {
    return 0;
}

int Send(char *buf, int bytes) {
    return 0;
}

int Close() {
    return 0;
}
