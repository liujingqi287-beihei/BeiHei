#include "board_pull.h"
#include "xdma_manager.h"
#include <iostream>
#include <vector>
#include <windows.h>

const unsigned kPcieBarRegStartRecv = 0x00000100;

BoardPull::BoardPull(unsigned targetBoardIndex,
    unsigned targetChannelIndex,
    SafeQueue<PacketBlockPtr>* targetFileQueue,
    SafeQueue<PacketBlockPtr>* targetNetQueue,
    unsigned targetChunkSize)
    : boardIndex(targetBoardIndex),
    channelIndex(targetChannelIndex),
    dmaChunkSize(targetChunkSize),
    fileQueuePtr(targetFileQueue),
    netQueuePtr(targetNetQueue),
    isRunning(false),
    totalPulledBytes(0),
    totalPulledFrames(0)
{
    if (!XdmaManager::getInstance().openReadChannel(boardIndex, channelIndex)) {
        std::cerr << "[BoardPull] 打开读通道失败! 板卡: " << boardIndex << ", 通道: " << channelIndex << "\n";
    }
}

BoardPull::~BoardPull()
{
    stopRead();
    XdmaManager::getInstance().closeReadChannel(boardIndex, channelIndex);
}

void BoardPull::enableStreamPush(bool isEnable)
{
    const unsigned kEnableValue = 1;
    const unsigned kDisableValue = 0;

    if (isEnable) {
        XdmaManager::getInstance().writePcieBarRegister(boardIndex, kPcieBarRegStartRecv, kDisableValue);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        XdmaManager::getInstance().writePcieBarRegister(boardIndex, kPcieBarRegStartRecv, kEnableValue);
    }
    else {
        XdmaManager::getInstance().writePcieBarRegister(boardIndex, kPcieBarRegStartRecv, kDisableValue);
    }
}

void BoardPull::startRead()
{
    if (isRunning) return;
    isRunning = true;
    workerThread = std::thread(&BoardPull::readStreamFromBoard, this);
}

void BoardPull::stopRead()
{
    if (!isRunning) return;
    isRunning = false;

    if (workerThread.joinable()) {
        workerThread.join();
    }

    if (fileQueuePtr) fileQueuePtr->notifyAllWaiters();
    if (netQueuePtr) netQueuePtr->notifyAllWaiters();

    enableStreamPush(false);
}

void BoardPull::readStreamFromBoard()
{
    std::vector<char> rawBuffer(dmaChunkSize);
    std::vector<char> deliveryChunk;
    deliveryChunk.reserve(dmaChunkSize * 8);

    enableStreamPush(true);

    while (isRunning.load()) {
        int readBytes = XdmaManager::getInstance().read_stream(boardIndex, channelIndex,
            rawBuffer.data(), dmaChunkSize);

        if (readBytes <= 0) {
            std::this_thread::yield();
            continue;
        }

        deliveryChunk.insert(deliveryChunk.end(),
            rawBuffer.begin(),
            rawBuffer.begin() + readBytes);

        // 指针偏移切块：彻底消灭频繁 vector::erase 引起的 CPU 拷贝瓶颈
        size_t processedOffset = 0;

        while (deliveryChunk.size() - processedOffset >= dmaChunkSize) {
            auto outFrame = std::make_shared<std::vector<char>>(
                deliveryChunk.begin() + processedOffset,
                deliveryChunk.begin() + processedOffset + dmaChunkSize
                );

            processedOffset += dmaChunkSize;
            totalPulledBytes += outFrame->size();
            totalPulledFrames++;

            // 广播仅拷贝 shared_ptr 引用计数，真正零深拷贝分发
            if (fileQueuePtr) fileQueuePtr->pushData(outFrame, isRunning);
            if (netQueuePtr)  netQueuePtr->pushData(outFrame, isRunning);
        }

        if (processedOffset > 0) {
            deliveryChunk.erase(deliveryChunk.begin(), deliveryChunk.begin() + processedOffset);
        }
    }

    // 退出前收尾：将不足一包的残留尾数据全量输出
    if (!deliveryChunk.empty()) {
        totalPulledBytes += deliveryChunk.size();
        totalPulledFrames++;

        auto tailFrame = std::make_shared<std::vector<char>>(std::move(deliveryChunk));
        if (fileQueuePtr) fileQueuePtr->pushData(tailFrame, isRunning);
        if (netQueuePtr)  netQueuePtr->pushData(tailFrame, isRunning);
    }

    enableStreamPush(false);
}