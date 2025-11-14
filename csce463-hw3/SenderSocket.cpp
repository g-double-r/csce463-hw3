#include "SenderSocket.h"
#include "pch.h"

using std::chrono::duration, std::chrono::duration_cast, std::chrono::high_resolution_clock,
    std::chrono::milliseconds, std::chrono::steady_clock, std::mutex, std::lock_guard, std::thread;

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
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

SenderSocket::~SenderSocket()
{
    closeSocket();
    delete[] buffer;
}

double SenderSocket::getElapsedTime()
{
    auto elapsedTime = steady_clock::now() - constructedTime;
    return duration_cast<duration<double>>(elapsedTime).count();
}

void SenderSocket::updateRTO(double RTT, DWORD ack)
{
    double alpha = 0.125, beta = 0.25;
    estRTT = (1 - alpha) * estRTT + alpha * RTT;
    devRTT = (1 - beta) * devRTT + beta * fabs(RTT - estRTT);

    RTO = estRTT + 4 * max(devRTT, 0.01);

    //if (RTT > 0.8) {
    //    printf("RTT = %.3f, estRTT = %.3f devRTT = %.3f, new RTO = %.3f from ACK %d. Base = %d\n", RTT, estRTT, devRTT, RTO, ack, senderBase);
    //    getchar();
    //}
}

int SenderSocket::Open(char *targetHost, short port, int senderWindow, LinkProperties *linkProperties)
{
    stats = thread(&SenderSocket::StatsRun, this);
    // TODO: sends SYN and receives SYN-ACK
    // send a packet with syn set to 1
    // should recv with syn and ack both to 1
    buffer = new Packet[senderWindow];
    window = senderWindow;
    // empty = CreateSemaphore(NULL, window, window, NULL);
    empty = CreateSemaphore(NULL, 0, window, NULL);
    full = CreateSemaphore(NULL, 0, window, NULL);

    socketReceiveReady = CreateEvent(NULL, false, false, NULL);
    eventQuit = CreateEvent(NULL, true, false, NULL);
    eventAllACKed = CreateEvent(NULL, true, false, NULL);
    long networkMask = FD_READ;
    int r = WSAEventSelect(sock, socketReceiveReady, networkMask);
    if (r == SOCKET_ERROR)
    {
        printf("WSAEventSelect() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

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
        remote.sin_addr.S_un.S_addr = IP;
    }

    // send
    // declare struct directly and read into it
    // sizeof sendersynheader when sending
    ReceiverHeader rh;
    sockaddr_in response;
    socklen_t respLen = sizeof(response);
    int count = 0;
    int nfds = (int)(sock + 1);
    while (count < maxAttempsSYN)
    {
        // send request to server
        double start = (double)clock() / CLOCKS_PER_SEC;
        // printf("[%.3f]  --> SYN %u (attempt %d of %d, RTO %.3f) to %s\n", start, ssh.sdh.seq, count + 1, maxAttempsSYN, RTO, inet_ntoa(remote.sin_addr));

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
        timeout.tv_sec = (long)RTO;
        timeout.tv_usec = (long)((RTO - timeout.tv_sec) * 1e6);
        fd_set fd;
        FD_ZERO(&fd);      // clear the set
        FD_SET(sock, &fd); // add your socket to the set

        int available = select(nfds, &fd, NULL, NULL, &timeout);
        if (available > 0)
        {
            int bytes = recvfrom(sock, (char *)(&rh), sizeof(SenderSynHeader), 0, (sockaddr *)&response, &respLen);
            if (bytes == SOCKET_ERROR)
            {
                printf("[%.3f]  <-- failed with %d on recvfrom()\n", getElapsedTime(), WSAGetLastError());
                return FAILED_RECV;
                // exit(EXIT_FAILURE);
            }

            // parse sdh
            // !! window size is always one for part 1
            if (rh.flags.SYN != 1 || rh.flags.ACK != 1)
            {
                printf("SYN-ACK not acknowledged!\n");
                exit(EXIT_FAILURE);
            }
            double end = (double)clock() / CLOCKS_PER_SEC;
            double delta = end - start;
            estRTT = delta;
            devRTT = 0;
            RTO = estRTT + 4 * max(devRTT, 0.01);
            // TODO: change window afer part1
            // printf("[%.3f]  <-- SYN-ACK %u window 1; setting initial RTO to %.3f\n", end, ssh.sdh.seq, RTO);
            worker = thread(&SenderSocket::WorkerRun, this);

            if (!ResetEvent(socketReceiveReady))
            {
                printf("ResetEvent() for socketReceiveREady failed with %d\n", WSAGetLastError());
                exit(EXIT_FAILURE);
            }

            lastReleased = min(window, (int)rh.recvWnd);
            effectiveWindow = lastReleased;
            if (!ReleaseSemaphore(empty, lastReleased, NULL)) {
                printf("ReleaseSemaphore() failed with %d\n", WSAGetLastError());
                exit(EXIT_FAILURE);
            }
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

void SenderSocket::sendPacket(const char *buf, const int &bytes)
{
    if (sendto(sock, buf, bytes, 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
    {
        printf("sendto() failed with %d\n", WSAGetLastError());
    }
}

void SenderSocket::WorkerRun()
{
    int kernelBuffer = 20e6; // 20 meg
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)(& kernelBuffer), sizeof(int)) == SOCKET_ERROR) {
        printf("setcokopt() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    kernelBuffer = 20e6; // 20 meg
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)(&kernelBuffer), sizeof(int)) == SOCKET_ERROR) {
        printf("setcokopt() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);


    HANDLE events[] = {socketReceiveReady, full, eventQuit};

    while (true)
    {
        DWORD timeout = INFINITE;
        if (senderBase != nextToSend)
        {
            timeout = (DWORD)((timerExpire - ((double)clock() / CLOCKS_PER_SEC)) * 1000);
        }
        int result = WaitForMultipleObjects(3, events, false, timeout);
        if (result == WAIT_FAILED || result == WAIT_ABANDONED)
        {
            printf("WaitForSingleObject() failed with %d\n", WSAGetLastError());
            exit(EXIT_FAILURE);
        }
        recomputeTimerExpire = false;
        switch (result)
        {
        case WAIT_TIMEOUT:
            recomputeTimerExpire = true;
            // resendbase
            {
                Packet *pkt = buffer + (senderBase % window);
                sendPacket(pkt->pkt, pkt->size);
            }
            ++baseRetxCount;
            ++timeoutCount;
            // printf("[worker @ %.3f] resent base with seq num %d\n", ((double)clock() / CLOCKS_PER_SEC), senderBase);
            break;
        case WAIT_OBJECT_0:
            recvPacket();
            break;
        case (WAIT_OBJECT_0 + 1):
        {
            Packet *pkt = buffer + (nextToSend % window);
            sendPacket(pkt->pkt, pkt->size);
            pkt->txTime = clock();
            // printf("[worker @ %.3f] sent packet with seq num %d\n", (double)pkt->txTime / CLOCKS_PER_SEC, nextToSend);

            if (nextToSend == senderBase)
            {
                recomputeTimerExpire = true;
            }

            ++nextToSend;
            break;
        }
        case (WAIT_OBJECT_0 + 2):
            return;
        default:
            printf("error encountered\n");
            exit(EXIT_FAILURE);
        }

        if (recomputeTimerExpire)
        {
            timerExpire = ((double)clock() / CLOCKS_PER_SEC) + RTO;
        }
    }
}

void SenderSocket::StatsRun()
{
    lastStatsTime = getElapsedTime();
    lastStatsBase = senderBase;
    while (WaitForSingleObject(eventQuit, 2000) == WAIT_TIMEOUT)
    {

        double now = getElapsedTime();
        double dt = now - lastStatsTime;
        DWORD b = senderBase;
        DWORD n = nextToSend;

        DWORD deltaAckPkts = (b >= lastStatsBase) ? (b - lastStatsBase) : 0;

        if (dt > 0)
        {
            goodput = (deltaAckPkts * 8 *(MAX_PKT_SIZE - sizeof(SenderDataHeader))) / (dt * 1e6);
        }

        double mbDelivered = totalAckedBytes / (1e6);

        printf("[%2d] B %5u ( %4.1f MB) N %5u T %d F %d W %u S %.3f Mbps RTT %.3f\n",
               (int)now,
               b,
               mbDelivered,
               n,
               timeoutCount,
               fastRetx,
               //(unsigned)min((DWORD)window, receiverWindow),
               (unsigned)effectiveWindow,
               goodput,
               estRTT);

        lastStatsTime = now;
        lastStatsBase = b;
    }
}

void SenderSocket::recvPacket()
{
    ReceiverHeader rh;
    int bytes = recvfrom(sock, (char *)(&rh), sizeof(ReceiverHeader), 0, NULL, NULL);
    if (bytes == SOCKET_ERROR)
    {
        printf("recvfrom() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }


    // check if FIN-ACK is recvd
    if (rh.flags.FIN == 1 && rh.flags.ACK == 1)
    {
        printf("[%.3f]  <-- FIN-ACK %u window %X\n", getElapsedTime(), rh.ackSeq, rh.recvWnd);
        SetEvent(eventQuit);
        return;
    }

    DWORD ack = rh.ackSeq;
    receiverWindow = rh.recvWnd;

    if (ack == seqNum) {
        SetEvent(eventAllACKed);
    }

    // printf("[worker @ %.3f] received ack with seq num %d\n", ((double)clock() / CLOCKS_PER_SEC), ack);

    Packet *pkt = buffer + ((ack - 1) % window);
    // printf("packet tx time: %.3f\n", ((double)(pkt->txTime) / CLOCKS_PER_SEC));
    double RTT = ((double)clock() / CLOCKS_PER_SEC) - ((double)(pkt->txTime) / CLOCKS_PER_SEC);
    //if (baseRetxCount == 0)
    //{
    //    updateRTO(RTT, ack);
    //}

    if (ack > senderBase)
    {
        if (baseRetxCount == 0)
        {
            updateRTO(RTT, ack);
        }

        dupACK = 0;
        baseRetxCount = 0;
        recomputeTimerExpire = true;
        DWORD newlyAcked = ack - senderBase;

        totalAckedBytes += newlyAcked * (MAX_PKT_SIZE - sizeof(SenderDataHeader));

        senderBase = ack;
        effectiveWindow = min(window, (int)rh.recvWnd);
        newReleased = senderBase + effectiveWindow - lastReleased;
        lastReleased += newReleased;

        if (!ReleaseSemaphore(empty, newReleased, NULL))
        {
            printf("ReleaseSemaphore() failed with %d on recv\n", WSAGetLastError());
            exit(EXIT_FAILURE);
        }
    }
    // this part is for triple duplicate ack
    else if (ack == senderBase) {
        // check counter and resend once it equals 3
        // same thing as timeout and reset variables
        ++dupACK;
        if (dupACK == 3) {
            Packet* pkt = buffer + (senderBase % window);
            sendPacket(pkt->pkt, pkt->size);
            recomputeTimerExpire = true;
            ++baseRetxCount;
            ++fastRetx;
            // printf("fast rext with base %d\n", senderBase);
        }
    }
}

int SenderSocket::Send(char *buf, int bytes)
{
    // follow the picture from class
    DWORD result = WaitForSingleObject(empty, INFINITE);
    if (result == WAIT_FAILED || result == WAIT_ABANDONED)
    {
        printf("WaitForSingleObject() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

        // build packet
        int pktSize = bytes + sizeof(SenderDataHeader);
        Packet *pkt = buffer + (seqNum % window);
        SenderDataHeader sdh;
        sdh.seq = seqNum;
        memcpy(pkt->pkt, &sdh, sizeof(SenderDataHeader));
        memcpy(pkt->pkt + sizeof(SenderDataHeader), buf, bytes);
        pkt->size = pktSize;
        //pkt->txTime = clock();

    result = ReleaseSemaphore(full, 1, NULL);
    if (result == 0)
    {
        printf("ReleaseSemaphore() failed with %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

    ++seqNum;

    return STATUS_OK;
}

int SenderSocket::Close(double &elapsedTime)
{

    WaitForSingleObject(eventAllACKed, INFINITE);

    elapsedTime = (double)clock() / CLOCKS_PER_SEC;



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
    ReceiverHeader rh;
    sockaddr_in response;
    socklen_t respLen = sizeof(response);
    int count = 0;
    int nfds = (int)(sock + 1);
    // todo: quit after max count
    while (WaitForSingleObject(eventQuit, (DWORD)(RTO * 1000)) == WAIT_TIMEOUT && count < maxAttempsFIN)
    {
        // send request to server
        double start = getElapsedTime();
        // printf("[%.3f]  --> FIN %u (attempt %d of %d, RTO %.3f)\n", start, sdh.seq, count + 1, maxAttempsFIN, RTO);

        if (sendto(sock, (char *)(&sdh), sizeof(SenderSynHeader), 0, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
        {
            printf("[%.3f]  --> failed with %d on sendto()\n", getElapsedTime(), WSAGetLastError());
            return FAILED_SEND;
            // WSACleanup();
            // exit(EXIT_FAILURE);
        }
        ++count;
    }

    if (count == maxAttempsFIN) {
        return TIMEOUT;
    }

    worker.join();
    stats.join();
    return STATUS_OK;
}

double SenderSocket::getEstRTT() {
    return estRTT;
}
