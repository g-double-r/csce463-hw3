/*
 * Giovan Ramirez-Rodarte
 * 432004695
 * CSCE 463 Fall 2025
 */

// csce463-hw3.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

using std::chrono::duration, std::chrono::duration_cast, std::chrono::high_resolution_clock, std::chrono::milliseconds;

static void initializeWinsock()
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

static void cleanUpWinsock()
{
    // call cleanup when done with everything and ready to exit program
    WSACleanup();
}

int main(int argc, char *argv[])
{
    // error check for 7 args
    if (argc != 8)
    {
        printf(
            "Incorrect Usage!\n\n"
            "Usage:\n"
            "    ./csce463-hw3{.exe} <destination_server> <buffer_size> <sender_window> <propagation_delay> <forward_loss> <return_loss> <bottleneck_speed>\n\n"
            "Arguments:\n"
            "    destination_server    Hostname or IP of the destination server\n"
            "    buffer_size           Power of 2 for buffer size\n"
            "    sender_window         Number of packets in the sender's window\n"
            "    propagation_delay     Propagation delay in seconds\n"
            "    forward_loss          Probability of packet loss in the forward direction\n"
            "    return_loss           Probability of packet loss in the return direction\n"
            "    bottleneck_speed      Bottleneck speed in Mbps\n");
        exit(EXIT_FAILURE);
    }

    initializeWinsock();

    // parse command-line parameters
    char *targetHost = argv[1];
    int power = atoi(argv[2]);
    int senderWindow = atoi(argv[3]);
    float propagationDelay = (float)atof(argv[4]);
    float forwardLoss = (float)atof(argv[5]);
    float returnLoss = (float)atof(argv[6]);
    int linkSpeed = atoi(argv[7]);

    printf("Main:   sender W = %d, RTT %.3f sec, loss %g / %g, link %d Mbps\n", senderWindow, propagationDelay, forwardLoss, returnLoss, linkSpeed);

    // initialize dword buffer
    uint64_t dwordBufSize = (uint64_t)1 << power;
    DWORD *dwordBuf = new DWORD[dwordBufSize];
    printf("Main:   initializing DWORD array with 2^%d elements... ", power);
    auto start = high_resolution_clock::now();
    for (uint64_t i = 0; i < dwordBufSize; ++i)
    {
        dwordBuf[i] = (DWORD)i;
    }
    auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start);
    printf("done in %lld ms\n", elapsed.count());

    // instantiate sendersocket class
    SenderSocket ss;

    // open connection
    LinkProperties lp;
    lp.RTT = propagationDelay;
    lp.speed = (float)(1e6 * linkSpeed);
    lp.pLoss[FORWARD_PATH] = forwardLoss;
    lp.pLoss[RETURN_PATH] = returnLoss;
    lp.bufferSize = (DWORD)(senderWindow + 5);
    start = high_resolution_clock::now();
    int status = ss.Open(targetHost, MAGIC_PORT, senderWindow, &lp);
    double secs = duration_cast<duration<double>>(high_resolution_clock::now() - start).count();
    start = high_resolution_clock::now();

    printf("Main:   ");
    switch (status)
    {
    case INVALID_NAME:
        printf("connect failed with status %d\n", status);
        delete[] dwordBuf;
        exit(EXIT_FAILURE);
    case FAILED_SEND:
        printf("connect failed with status %d\n", status);
        delete[] dwordBuf;
        exit(EXIT_FAILURE);
    case FAILED_RECV:
        printf("connect failed with status %d\n", status);
        delete[] dwordBuf;
        exit(EXIT_FAILURE);
    case TIMEOUT:
        printf("connect failed with status %d\n", status);
        delete[] dwordBuf;
        exit(EXIT_FAILURE);
    default:
        printf("connected to %s in %.3f sec, pkt size %d bytes\n", targetHost, secs, MAX_PKT_SIZE);
        break;
    }

    // TODO: uncomment for part 2
    // send loop
    char *charBuf = (char *)dwordBuf;
    uint64_t byteBufferSize = dwordBufSize << 2;

    uint64_t off = 0; // current position in buffer
    while (off < byteBufferSize)
    {
        // decide the size of next chunk
        int bytes = (int)min((byteBufferSize - off), (uint64_t)(MAX_PKT_SIZE - sizeof(SenderDataHeader)));
        // send chunk into socket
        if ((status = ss.Send(charBuf + off, bytes)) != STATUS_OK)
        {
            // error handing: print status and quit
            printf("send failed with status %d\n", status);
            delete[] dwordBuf;
            cleanUpWinsock();
            exit(EXIT_FAILURE);
        }
        off += bytes;
    }

    // close connection
    secs = duration_cast<duration<double>>(high_resolution_clock::now() - start).count();
    status = ss.Close();
    if (status != STATUS_OK)
    {
        printf("close failed with status %d\n", status);
        exit(EXIT_FAILURE);
    }

    Checksum cs;
    DWORD chkSum = cs.CRC32((unsigned char*)charBuf, byteBufferSize);
    double measuredRate = ((dwordBufSize * 32) / (1e3)) / secs;
    printf("Main:   transfer finished in %.3f sec, %.2f Kbps, checksum %X\n", secs, measuredRate, chkSum);

    double estRTT = ss.getEstRTT();
    double idealRate = ((MAX_PKT_SIZE - sizeof(SenderDataHeader)) * 8 * senderWindow) / (estRTT * 1e3);
    printf("Main:   estRTT %.3f, ideal rate %.2f Kbps\n", estRTT, idealRate);


    cleanUpWinsock();

    delete[] dwordBuf;
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
