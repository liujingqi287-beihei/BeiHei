#pragma once

#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include "../board/safe_queue.h"

#pragma comment(lib, "ws2_32.lib")

class NetOutput {
public:
    NetOutput(unsigned short listenPort, SafeQueue<std::vector<char>>& sourceQueue);
    ~NetOutput();

    void startSend();
    void stopSend();
    bool waitForClientConnect(unsigned timeoutMs = 30000);

private:
    void networkSendLoop();
    bool initializeSocket();
    void closeSocket();

    unsigned short port;
    SafeQueue<std::vector<char>>& inQueue;
    std::atomic<bool> isRunning;
    std::atomic<bool> isClientConnected;
    std::thread workerThread;

    SOCKET listenSocket;
    SOCKET clientSocket;
};