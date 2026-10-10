#include "file_output.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

FileOutput::FileOutput(const std::string& targetFilePath, SafeQueue<PacketBlockPtr>& sourceQueue)
    : saveFilePath(targetFilePath), inQueue(sourceQueue), isRunning(false)
{
    if (saveFilePath.empty()) return;

    prepareTargetDirectory(saveFilePath);
    outputFile.open(saveFilePath, std::ios::binary | std::ios::out | std::ios::app);
    if (!outputFile.is_open()) {
        std::cerr << "[FileOutput] 创建落地输出文件失败: " << saveFilePath << "\n";
    }
}

FileOutput::~FileOutput()
{
    stopWrite();
    if (outputFile.is_open()) {
        outputFile.close();
    }
}

void FileOutput::prepareTargetDirectory(const std::string& targetFilePath)
{
    try {
        fs::path pathObject(targetFilePath);
        if (pathObject.has_parent_path() && !fs::exists(pathObject.parent_path())) {
            fs::create_directories(pathObject.parent_path());
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[FileOutput] 目录创建异常: " << e.what() << "\n";
    }
}

void FileOutput::startWrite()
{
    if (isRunning) return;
    isRunning = true;
    workerThread = std::thread(&FileOutput::writeDataToFile, this);
}

void FileOutput::stopWrite()
{
    if (!isRunning) return;
    isRunning = false;
    inQueue.notifyAllWaiters();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void FileOutput::writeDataToFile()
{
    const uint64_t kFlushIntervalBytes = 32 * 1024 * 1024; // 适度扩大缓冲，减少磁盘冲刷抖动
    uint64_t unwrittenCacheBytes = 0;

    while (isRunning.load() || inQueue.getQueueSize() > 0) {
        PacketBlockPtr dataBlock = nullptr;
        if (!inQueue.popData(dataBlock, isRunning, 50)) continue;
        if (!dataBlock || dataBlock->empty()) continue;

        if (outputFile.is_open()) {
            outputFile.write(dataBlock->data(), dataBlock->size());
            unwrittenCacheBytes += dataBlock->size();

            if (unwrittenCacheBytes >= kFlushIntervalBytes) {
                outputFile.flush();
                unwrittenCacheBytes = 0;
            }
        }
    }

    if (outputFile.is_open()) {
        outputFile.flush();
    }
}