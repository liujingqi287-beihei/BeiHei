#pragma once
#include <thread>
#include <atomic>
#include <string>
#include <fstream>
#include "safe_queue.h"
#include "file_input.h"

class BoardPush {
public:
    BoardPush(unsigned boardIdx,
        unsigned channelIdx,
        SafeQueue<FramePacketRef>& inputQueue,
        const std::string& mirrorDumpPath = "");
    ~BoardPush();

    void startWrite();
    void stopWrite();

    unsigned long long getSentFrameCount() const { return sentFrameCount.load(); }
    unsigned long long getDroppedFrameCount() const { return droppedFrameCount.load(); }
    uint64_t getTotalSentBytes() const { return totalSentBytes.load(); }

private:
    void pushWorkerLoop();
    void prepareTargetDirectory(const std::string& targetFilePath);

    unsigned boardIndex;
    unsigned channelIndex;
    SafeQueue<FramePacketRef>& sendQueue;
    std::string mirrorFilePath;
    std::ofstream mirrorFile;

    std::atomic<bool> isRunning;
    std::thread workerThread;

    std::atomic<unsigned long long> sentFrameCount{ 0 };
    std::atomic<unsigned long long> droppedFrameCount{ 0 };
    std::atomic<uint64_t> totalSentBytes{ 0 };
};