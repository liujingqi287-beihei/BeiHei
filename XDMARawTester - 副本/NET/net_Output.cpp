#include "net_output.h"
#include <iostream>

const int kSocketSendBufferSize = 4 * 1024 * 1024; // 4MB 发送缓冲

NetOutput::NetOutput(unsigned short listenPort, SafeQueue<std::vector<char>>& sourceQueue)
    : port(listenPort),
    inQueue(sourceQueue),
    isRunning(false),
    isClientConnected(false),
    listenSocket(INVALID_SOCKET),
    clientSocket(INVALID_SOCKET)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[NetOutput] WSAStartup 初始化失败!\n";
    }
}

NetOutput::~NetOutput()
{
    stopSend();
    WSACleanup();
}

bool NetOutput::initializeSocket()
{
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "[NetOutput] 创建 Socket 失败: " << WSAGetLastError() << "\n";
        return false;
    }

    int optVal = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, sizeof(optVal));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    // 使用现代标准 API inet_pton 转换 IP 地址，杜绝 C4996 报错
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[NetOutput] 绑定本地端口 " << port << " 失败: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "[NetOutput] 监听端口失败: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    return true;
}

bool NetOutput::waitForClientConnect(unsigned timeoutMs)
{
    if (listenSocket == INVALID_SOCKET) {
        if (!initializeSocket()) return false;
    }

    std::cout << "[NetOutput] 正在监听 127.0.0.1:" << port << "，等待 C# 对比程序连接...\n";

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSocket, &readSet);

    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int selectRet = select(0, &readSet, nullptr, nullptr, (timeoutMs == 0 ? nullptr : &tv));
    if (selectRet <= 0) {
        std::cerr << "[NetOutput] 等待客户端连接超时。\n";
        return false;
    }

    clientSocket = accept(listenSocket, nullptr, nullptr);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[NetOutput] 接受连接失败: " << WSAGetLastError() << "\n";
        return false;
    }

    setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, (char*)&kSocketSendBufferSize, sizeof(kSocketSendBufferSize));
    int noDelay = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay));

    isClientConnected = true;
    std::cout << "[NetOutput] C# 对比程序已成功建立 TCP 连接，进入推流状态！\n";
    return true;
}

void NetOutput::startSend()
{
    if (isRunning) return;

    isRunning = true;
    workerThread = std::thread(&NetOutput::networkSendLoop, this);
}

void NetOutput::stopSend()
{
    if (!isRunning) return;

    isRunning = false;
    inQueue.notifyAllWaiters();
    closeSocket();

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void NetOutput::closeSocket()
{
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    isClientConnected = false;
}

void NetOutput::networkSendLoop()
{
    const size_t kEmptyQueueThreshold = 0;

    while (isRunning || inQueue.getQueueSize() > kEmptyQueueThreshold) {
        std::vector<char> dataBuffer;
        if (!inQueue.popData(dataBuffer, isRunning)) {
            continue;
        }

        if (dataBuffer.empty()) continue;

        if (isClientConnected && clientSocket != INVALID_SOCKET) {
            size_t totalSent = 0;
            size_t packetSize = dataBuffer.size();
            const char* rawPtr = dataBuffer.data();

            while (totalSent < packetSize && isRunning) {
                int bytesSent = send(clientSocket, rawPtr + totalSent, static_cast<int>(packetSize - totalSent), 0);
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "\n[NetOutput] 网络连接中断或对端已关闭!\n";
                    isClientConnected = false;
                    break;
                }
                totalSent += bytesSent;
            }
        }
    }
}