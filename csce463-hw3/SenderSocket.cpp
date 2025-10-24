#include "SenderSocket.h"
#include "pch.h"

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

SenderSocket::SenderSocket()
{
    // open UDP socket and bind
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        printf("socket() generated error %d\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(0);
    // bind UDP socket
    if (bind(sock, (sockaddr *)&local, sizeof(local)) == SOCKET_ERROR)
    {
        printf("bind() generated error %d\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    constructedTime = steady_clock::now();
}

void SenderSocket::closeSocket()
{
#ifdef _WIN32
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
#else
    if (sock != INVALID_SOCKET)
    {
        close(sock);
        sock = INVALID_SOCKET;
    }
#endif
}

SenderSocket::~SenderSocket()
{
    closeSocket();
}

double SenderSocket::getElapsedTime()
{
    auto elapsedTime = steady_clock::now() - constructedTime;
    return duration_cast<duration<double>>(elapsedTime).count();
}

int SenderSocket::Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties)
{
    // TODO: sends SYN and receives SYN-ACK
    // send a packet with syn set to 1
    // should recv with syn and ack both to 1

    // prepare packet to send
    SenderSynHeader ssh;
    ssh.sdh.flags.SYN = 1;
    memcpy(&ssh.lp, linkProperties, sizeof(LinkProperties));
    ssh.sdh.seq = 0;

    // locate destination
    sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = port;

    hostent *host = gethostbyname(targetHost);
    if (!host)
    {
        return INVALID_NAME;
    }
    memcpy((char *)&(remote.sin_addr), host->h_addr, host->h_length);

    // send
    // declare struct directly and read into it
    // sizeof sendersynheader when sending
    SenderSynHeader sshRecv;
    sockaddr_in response;
    socklen_t respLen = sizeof(response);
    int count = 1;
    int nfds = (int)(sock + 1);
    while (count <= maxAttempsSYN)
    {
        // send request to server
        printf("[%.3f]  --> SYN ", getElapsedTime());

        auto start = high_resolution_clock::now();
        if (sendto(sock, &ssh, sizeof(SenderSynHeader), 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
        {
            printf("failed with %d on sendto()\n", WSAGetLastError());
            return FAILED_SEND;
            // WSACleanup();
            // exit(EXIT_FAILURE);
        }

        // prepare to receive
        // TODO: tie to RTO?
        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        fd_set fd;
        FD_ZERO(&fd);      // clear the set
        FD_SET(sock, &fd); // add your socket to the set

        int available = select(nfds, &fd, NULL, NULL, &timeout);
        if (available > 0)
        {
            int bytes = recvfrom(sock, &sshRecv, sizeof(SenderSynHeader), 0, (sockaddr *)&response, &respLen);
            if (bytes == SOCKET_ERROR)
            {
                printf("failed with %d on recvfrom()\n", WSAGetLastError());
                return FAILED_RECV;
                // exit(EXIT_FAILURE);
            }

            double delta = duration_cast<duration<double>>(high_resolution_clock::now() - start).count();
            // parse sdh
            // !! window size is always one for part 1
            if (sshRecv.sdh.flags.SYN != 1 || sshRecv.sdh.flags.ACK != 1)
            {
                printf("SYN-ACK not acknowledged!\n");
                exit(EXIT_FAILURE);
            }
            printf("%u (attempt %d of %d, RTO %.3f) to %s\n", ssh.sdh.seq, count, maxAttempsSYN, RTO, inet_ntoa(remote.sin_addr));
            RTO -= delta;
            printf("[%.3f]  --> SYN-ACK %u window %d; setting initial RTO to %.3f\n", getElapsedTime(), sshRecv.sdh.seq, window, RTO);
        }
        else if (available == SOCKET_ERROR)
        {
            printf("failed with  %d on select\n", WSAGetLastError());
        }
        else if (available == 0)
        {
            printf("failed with connection timeout in 1000ms\n");
            ++count;
            continue;
        }
        else
        {
            printf("failed with other error %d\n", WSAGetLastError());
            exit(EXIT_FAILURE);
        }
        ++count;
    }

    if (count == maxAttempsSYN)
    {
        return TIMEOUT;
    }

    return STATUS_OK;
}

int SenderSocket::Send(char *buf, int bytes)
{
    return 0;
}

int SenderSocket::Close()
{
    // TODO: sends FIN and receieves FIN-ACK
    return 0;
}

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
