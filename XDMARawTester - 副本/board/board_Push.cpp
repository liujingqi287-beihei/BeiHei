#include "board_push.h"
#include "xdma_manager.h"
#include <iostream>
#include <thread>
#include <immintrin.h>
#include <filesystem>
namespace fs = std::filesystem;

void BoardPush::prepareTargetDirectory(const std::string& targetFilePath) {
    try {
        fs::path pathObject(targetFilePath);
        if (pathObject.has_parent_path() && !fs::exists(pathObject.parent_path())) {
            fs::create_directories(pathObject.parent_path());
        }
    }
    catch (...) {}
}
BoardPush::BoardPush(unsigned boardIdx,
    unsigned channelIdx,
    SafeQueue<FramePacketRef>& inputQueue,
    const std::string& mirrorDumpPath)
    : boardIndex(boardIdx),
    channelIndex(channelIdx),
    sendQueue(inputQueue),
    mirrorFilePath(mirrorDumpPath),
    isRunning(false),
    sentFrameCount(0),
    droppedFrameCount(0)
{
    // 如果指定了镜像文件路径，则打开文件
    if (!mirrorFilePath.empty()) {
        prepareTargetDirectory(mirrorFilePath);
        mirrorFile.open(mirrorFilePath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!mirrorFile.is_open()) {
            std::cerr << "[BoardPush] 创建发送镜像文件失败: " << mirrorFilePath << "\n";
        }
    }

    if (!XdmaManager::getInstance().openWriteChannel(boardIndex, channelIndex)) {
        std::cerr << "[BoardPush] 打开写通道失败! 板卡: " << boardIndex << ", 通道: " << channelIndex << "\n";
    }
}

BoardPush::~BoardPush()
{
    stopWrite();
    if (mirrorFile.is_open()) {
        mirrorFile.flush();
        mirrorFile.close();
    }
    XdmaManager::getInstance().closeWriteChannel(boardIndex, channelIndex);
}

void BoardPush::startWrite()
{
    if (isRunning) return;
    isRunning = true;
    workerThread = std::thread(&BoardPush::pushWorkerLoop, this);
}

void BoardPush::stopWrite()
{
    if (!isRunning) return;
    isRunning = false;
    sendQueue.notifyAllWaiters();
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (mirrorFile.is_open())
    {
        mirrorFile.flush();
    }
}

void BoardPush::pushWorkerLoop()
{
    int backoffCount = 0;

    while (isRunning.load()) {
        FramePacketRef frame;

        if (sendQueue.popData(frame, isRunning, 50)) {
            if (frame.size == 0 || !frame.dataPtr) continue;

            size_t bytesSent = 0;

            while (bytesSent < frame.size && isRunning.load()) {
                unsigned int toSend = static_cast<unsigned int>(frame.size - bytesSent);

                int writtenBytes = XdmaManager::getInstance().write_stream(
                    boardIndex,
                    channelIndex,
                    const_cast<char*>(frame.dataPtr + bytesSent),
                    toSend
                );

                if (writtenBytes > 0) {
                    bytesSent += static_cast<size_t>(writtenBytes);
                    backoffCount = 0; // 发送成功，重置退避计数
                }
                else {
                    // 自适应退避：硬件背压时梯级降温，防止单核 CPU 100% 空转
                    backoffCount++;
                    if (backoffCount < 16) {
                        _mm_pause();
                    }
                    else if (backoffCount < 64) {
                        std::this_thread::yield();
                    }
                    else {
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                }
            }

            if (bytesSent == frame.size) {
                sentFrameCount++;
                totalSentBytes += frame.size;

                if (mirrorFile.is_open())
                {
                    mirrorFile.write(frame.dataPtr, frame.size);
                }
            }
            else {
                droppedFrameCount++;
            }
        }
    }
}
