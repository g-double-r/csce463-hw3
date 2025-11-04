#include "SenderSocket.h"
#include "pch.h"

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::mutex;
using std::lock_guard;

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

    // setup remote
    memset(&remote, 0, sizeof(remote));

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

double SenderSocket::curRTO()
{
    return (estRTT + 4 * max(devRTT, 0.01));
}

int SenderSocket::Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties)
{
    // TODO: sends SYN and receives SYN-ACK
    // send a packet with syn set to 1
    // should recv with syn and ack both to 1
    buffer = new Packet[senderWindow];
    window = senderWindow;
    empty = CreateSemaphore(NULL, window, window, NULL);
    full = CreateSemaphore(NULL, 0, window, NULL);

    RTO = max(1.0, (double)(2 * linkProperties->RTT));
    estRTT = linkProperties->RTT;

    // error check for already open
    sockaddr_in zeroAddr;
    memset(&zeroAddr, 0, sizeof(zeroAddr));
    if (memcmp(&zeroAddr, &remote, sizeof(sockaddr_in)) != 0)
    {
        return ALREADY_CONNECTED;
    }

    // prepare packet to send
    SenderSynHeader ssh;
    ssh.sdh.flags.SYN = 1;
    memcpy(&ssh.lp, linkProperties, sizeof(LinkProperties));
    ssh.sdh.seq = seqNum;

    // locate destination
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);

    DWORD IP = inet_addr(targetHost);
    if (IP == INADDR_NONE)
    {
        hostent *host = gethostbyname(targetHost);
        if (!host)
        {
            printf("[%.3f]  --> target %s is invalid\n", getElapsedTime(), targetHost);
            return INVALID_NAME;
        }
        memcpy((char *)&(remote.sin_addr), host->h_addr, host->h_length);
    }
    else
    {
#ifdef _WIN32
        remote.sin_addr.S_un.S_addr = IP;
#else
        remote.sin_addr.s_addr = IP;
#endif
    }

    // send
    // declare struct directly and read into it
    // sizeof sendersynheader when sending
    SenderSynHeader sshRecv;
    sockaddr_in response;
    socklen_t respLen = sizeof(response);
    int count = 0;
    int nfds = (int)(sock + 1);
    while (count < maxAttempsSYN)
    {
        // send request to server
        double start = getElapsedTime();
        printf("[%.3f]  --> SYN %u (attempt %d of %d, RTO %.3f) to %s\n", start, ssh.sdh.seq, count + 1, maxAttempsSYN, RTO, inet_ntoa(remote.sin_addr));

        if (sendto(sock, (char *)(&ssh), sizeof(SenderSynHeader), 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
        {
            printf("[%.3f]  --> failed with %d on sendto()\n", getElapsedTime(), WSAGetLastError());
            return FAILED_SEND;
            // WSACleanup();
            // exit(EXIT_FAILURE);
        }

        // prepare to receive
        // TODO: tie to RTO?
        timeval timeout;
#ifdef __APPLE__
        timeout.tv_sec = (time_t)RTO;
        timeout.tv_usec = (suseconds_t)((RTO - timeout.tv_sec) * 1e6);
#else
        timeout.tv_sec = (long)RTO;
        timeout.tv_usec = (long)((RTO - timeout.tv_sec) * 1e6);
#endif
        fd_set fd;
        FD_ZERO(&fd);      // clear the set
        FD_SET(sock, &fd); // add your socket to the set

        int available = select(nfds, &fd, NULL, NULL, &timeout);
        if (available > 0)
        {
            int bytes = recvfrom(sock, (char *)(&sshRecv), sizeof(SenderSynHeader), 0, (sockaddr *)&response, &respLen);
            if (bytes == SOCKET_ERROR)
            {
                printf("[%.3f]  <-- failed with %d on recvfrom()\n", getElapsedTime(), WSAGetLastError());
                return FAILED_RECV;
                // exit(EXIT_FAILURE);
            }

            // parse sdh
            // !! window size is always one for part 1
            if (sshRecv.sdh.flags.SYN != 1 || sshRecv.sdh.flags.ACK != 1)
            {
                printf("SYN-ACK not acknowledged!\n");
                exit(EXIT_FAILURE);
            }
            double end = getElapsedTime();
            double delta = getElapsedTime() - start;
            RTO = (3 * delta);
            // TODO: change window afer part1
            printf("[%.3f]  <-- SYN-ACK %u window 1; setting initial RTO to %.3f\n", end, ssh.sdh.seq, RTO);
            return STATUS_OK;
        }
        else if (available == SOCKET_ERROR)
        {
            printf("[%.3f]  <-- failed with %d on select()\n", getElapsedTime(), WSAGetLastError());
            exit(EXIT_FAILURE);
        }
        else if (available == 0)
        {
            // printf("failed with connection timeout in 1000ms\n");
            ++count;
            continue;
        }
        else
        {
            printf("[%.3f]  <-- failed with other error %d\n", getElapsedTime(), WSAGetLastError());
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

int SenderSocket::sendPacket(const char *buf, int &bytes)
{
    if (sendto(sock, buf, bytes, 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
    {
        return FAILED_SEND;
    }

    return STATUS_OK;
}

void SenderSocket::worker() {

}

int SenderSocket::Send(char *buf, int bytes)
{
    // follow the picture from class
    DWORD result = WaitForSingleObject(empty, INFINITE);
    if (result == WAIT_FAILED || result == WAIT_ABANDONED) {
        printf("WaitForSingleObject() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

    {
        lock_guard<mutex> lg(mtx);
        // build packet
        int pktSize = bytes + sizeof(SenderDataHeader);
        Packet* pkt = buffer + (seqNum % window);
        SenderDataHeader sdh;
        sdh.seq = seqNum;
        memcpy(pkt->pkt, &sdh, sizeof(SenderDataHeader));
        memcpy(pkt->pkt + sizeof(SenderDataHeader), buf, bytes);
        pkt->size = pktSize;
        pkt->txTime = clock();
    }

    result = ReleaseSemaphore(full, 1, NULL);
    if (result == 0) {
        printf("ReleaseSemaphore() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

    ++seqNum;

    // at the very end we make the worker and call the fucntion

    

//     // stitch SDH + packet
//     // keep track of sequence numbers
//     sockaddr_in zeroAddr;
//     memset(&zeroAddr, 0, sizeof(zeroAddr));
//     if (memcmp(&zeroAddr, &remote, sizeof(sockaddr_in)) == 0)
//     {
//         return NOT_CONNECTED;
//     }

//     // build packet
//     int pktSize = bytes + sizeof(SenderDataHeader);
//     char *pkt = new char[pktSize];
//     SenderDataHeader *sdh = (SenderDataHeader *)(pkt);
//     sdh->seq = nextSeqNum;
//     memcpy((pkt + sizeof(SenderDataHeader)), buf, bytes);

//     // send once and start timer
//     if (sendPacket(pkt, pktSize) != STATUS_OK)
//     {
//         printf("failed with %d on sendto()\n", WSAGetLastError());
//         delete[] pkt;
//         return FAILED_SEND;
//     }
//     RTO = curRTO();
//     auto sendTime = steady_clock::now();
//     state = (nextSeqNum == 0) ? SendState::WAIT_ACK_0 : SendState::WAIT_ACK_1;

//     // sequence number are 4 bytes in len
//     char recvBuf[4];
//     sockaddr_in response;
//     socklen_t respLen = sizeof(response);
//     int count = 0;
//     int nfds = (int)(sock + 1);
//     while (true)
//     {
//         // prepare to receive
//         timeval timeout;
// #ifdef __APPLE__
//         timeout.tv_sec = (time_t)RTO;
//         timeout.tv_usec = (suseconds_t)((RTO - timeout.tv_sec) * 1e6);
// #else
//         timeout.tv_sec = (long)RTO;
//         timeout.tv_usec = (long)((RTO - timeout.tv_sec) * 1e6);
// #endif
//         fd_set fd;
//         FD_ZERO(&fd);      // clear the set
//         FD_SET(sock, &fd); // add your socket to the set

//         int available = select(nfds, &fd, NULL, NULL, &timeout);
//         if (available > 0)
//         {
//             int bytes = recvfrom(sock, recvBuf, 4, 0, (sockaddr *)&response, &respLen);
//             if (bytes == SOCKET_ERROR)
//             {
//                 printf("failed with %d on recvfrom()\n", WSAGetLastError());
//                 delete[] pkt;
//                 return FAILED_RECV;
//             }

//             // extract seq number
//             DWORD seqNum = 0;
//             memcpy(&seqNum, recvBuf, sizeof(DWORD));
//             if (seqNum == nextSeqNum)
//             {
//                 // compute new RTT estimate per slides
//                 double sample = duration<double>(high_resolution_clock::now() - sendTime).count();
//                 const double alpha = 0.125, beta = 0.25;
//                 estRTT = (1 - alpha) * estRTT + (alpha * sample);
//                 devRTT = (1 - beta) * devRTT + (beta * std::fabs(sample = estRTT));

//                 // update global RTO
//                 RTO = curRTO();
//                 nextSeqNum ^= 1; // this flips the LSB from 0 to 1 and back since we only deal with those two for this part
//                 state = (nextSeqNum == 0) ? SendState::WAIT_CALL_0 : SendState::WAIT_CALL_1;
//                 delete[] pkt;
//                 return STATUS_OK;
//             }
//         }
//         else if (available == SOCKET_ERROR)
//         {
//             printf("failed with %d on select()\n", WSAGetLastError());
//             delete[] pkt;
//             exit(EXIT_FAILURE);
//         }
//         else if (available == 0)
//         {
//             // timeout, fast retx
//             if (sendPacket(pkt, pktSize) != STATUS_OK)
//             {
//                 printf("failed with %d on sendto()\n", WSAGetLastError());
//                 delete[] pkt;
//                 return FAILED_SEND;
//             }
//             RTO = curRTO();
//             continue;
//         }
//         else
//         {
//             printf("failed with other error %d\n", WSAGetLastError());
//             delete[] pkt;
//             exit(EXIT_FAILURE);
//         }
//     }

    return 0;
}

int SenderSocket::Close()
{

    // TODO: call threads to die
    // TODO: sends FIN and receieves FIN-ACK
    // prepare packet to send
    SenderDataHeader sdh;
    sdh.flags.FIN = 1;
    sdh.seq = seqNum;

    // error check
    sockaddr_in zeroAddr;
    memset(&zeroAddr, 0, sizeof(zeroAddr));
    if (memcmp(&zeroAddr, &remote, sizeof(sockaddr_in)) == 0)
    {
        return NOT_CONNECTED;
    }
    // send
    // declare struct directly and read into it
    // sizeof sendersynheader when sending
    SenderDataHeader sdhRecv;
    sockaddr_in response;
    socklen_t respLen = sizeof(response);
    int count = 0;
    int nfds = (int)(sock + 1);
    while (count < maxAttempsFIN)
    {
        // send request to server
        double start = getElapsedTime();
        printf("[%.3f]  --> FIN %u (attempt %d of %d, RTO %.3f)\n", start, sdh.seq, count + 1, maxAttempsFIN, RTO);

        if (sendto(sock, (char *)(&sdh), sizeof(SenderSynHeader), 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
        {
            printf("[%.3f]  --> failed with %d on sendto()\n", getElapsedTime(), WSAGetLastError());
            return FAILED_SEND;
            // WSACleanup();
            // exit(EXIT_FAILURE);
        }

        // prepare to receive
        // TODO: tie to RTO?
        timeval timeout;
#ifdef __APPLE__
        timeout.tv_sec = (time_t)RTO;
        timeout.tv_usec = (suseconds_t)((RTO - timeout.tv_sec) * 1e6);
#else
        timeout.tv_sec = (long)RTO;
        timeout.tv_usec = (long)((RTO - timeout.tv_sec) * 1e6);
#endif
        fd_set fd;
        FD_ZERO(&fd);      // clear the set
        FD_SET(sock, &fd); // add your socket to the set

        int available = select(nfds, &fd, NULL, NULL, &timeout);
        if (available > 0)
        {
            int bytes = recvfrom(sock, (char *)(&sdhRecv), sizeof(SenderSynHeader), 0, (sockaddr *)&response, &respLen);
            if (bytes == SOCKET_ERROR)
            {
                printf("[%.3f]  <-- failed with %d on recvfrom()\n", getElapsedTime(), WSAGetLastError());
                return FAILED_RECV;
                // exit(EXIT_FAILURE);
            }

            // parse sdh
            // !! window size is always one for part 1
            if (sdhRecv.flags.FIN != 1 || sdhRecv.flags.ACK != 1)
            {
                printf("FIN-ACK not acknowledged!\n");
                exit(EXIT_FAILURE);
            }
            double end = getElapsedTime();
            // TODO: change window afer part1
            printf("[%.3f]  <-- FIN-ACK %u window 0\n", end, sdh.seq);
            return STATUS_OK;
        }
        else if (available == SOCKET_ERROR)
        {
            printf("[%.3f]  <-- failed with %d on select()\n", getElapsedTime(), WSAGetLastError());
            exit(EXIT_FAILURE);
        }
        else if (available == 0)
        {
            // printf("failed with connection timeout in 1000ms\n");
            ++count;
            continue;
        }
        else
        {
            printf("[%.3f]  <-- failed with other error %d\n", getElapsedTime(), WSAGetLastError());
            exit(EXIT_FAILURE);
        }
        ++count;
    }

    if (count == maxAttempsFIN)
    {
        return TIMEOUT;
    }

    return STATUS_OK;
}
