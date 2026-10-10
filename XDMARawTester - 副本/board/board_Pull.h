#pragma once
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "safe_queue.h"

// 接收端统一输出智能指针块，实现零深拷贝广播
using PacketBlockPtr = std::shared_ptr<const std::vector<char>>;

class BoardPull {
public:
    BoardPull(unsigned targetBoardIndex,
        unsigned targetChannelIndex,
        SafeQueue<PacketBlockPtr>* targetFileQueue,
        SafeQueue<PacketBlockPtr>* targetNetQueue,
        unsigned targetChunkSize);
    ~BoardPull();

    void startRead();
    void stopRead();

    uint64_t getTotalPulledBytes() const { return totalPulledBytes.load(); }
    uint64_t getTotalPulledFrames() const { return totalPulledFrames.load(); }

private:
    void readStreamFromBoard();
    void enableStreamPush(bool isEnable);

    unsigned boardIndex;
    unsigned channelIndex;
    unsigned dmaChunkSize;
    SafeQueue<PacketBlockPtr>* fileQueuePtr;
    SafeQueue<PacketBlockPtr>* netQueuePtr;
    std::atomic<bool> isRunning;
    std::thread workerThread;

    std::atomic<uint64_t> totalPulledBytes;
    std::atomic<uint64_t> totalPulledFrames;
};