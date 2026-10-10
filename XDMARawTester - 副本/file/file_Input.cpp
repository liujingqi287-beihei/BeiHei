#include "file_input.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <windows.h>
#include <immintrin.h>

namespace fs = std::filesystem;

// 混合分段微秒精确延时：大段睡眠让出 CPU，碎片时间自旋对齐
static void hybridPreciseDelay(double microseconds, const std::atomic<bool>& isRunning) {
    if (microseconds <= 0.0) return;

    LARGE_INTEGER frequency, start, current;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    // 大于 1.5ms 时，先行 Sleep 让出 CPU 调度
    if (microseconds > 1500.0) {
        DWORD sleepMs = static_cast<DWORD>((microseconds - 1000.0) / 1000.0);
        Sleep(sleepMs);
    }

    double targetTicks = (microseconds * static_cast<double>(frequency.QuadPart)) / 1000000.0;
    while (isRunning.load()) {
        QueryPerformanceCounter(&current);
        if (static_cast<double>(current.QuadPart - start.QuadPart) >= targetTicks) {
            break;
        }
        _mm_pause();
    }
}

FileInput::FileInput(SafeQueue<FramePacketRef>& targetQueue)
    : outQueue(targetQueue), isRunning(false) {}

FileInput::~FileInput() {
    stopRead();
}

void FileInput::startRead(const std::string& folderPath, unsigned loopCount, double delayMs) {
    if (isRunning) return;
    isRunning = true;
    workerThread = std::thread(&FileInput::readFileToQueue, this, folderPath, loopCount, delayMs);
}

void FileInput::stopRead() {
    if (!isRunning) return;
    isRunning = false;
    outQueue.notifyAllWaiters();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void FileInput::waitForCompletion() {
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

bool FileInput::loadFilesFromDisk(const std::string& folderPath, std::vector<AlignedBuffer>& fileBufferList) {
    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        std::cerr << "[FileInput] 输入路径不存在或非目录: " << folderPath << "\n";
        return false;
    }

    std::vector<std::string> pathList;
    for (const auto& fileItem : fs::directory_iterator(folderPath)) {
        if (fileItem.is_regular_file()) {
            pathList.push_back(fileItem.path().string());
        }
    }

    if (pathList.empty()) return false;
    std::sort(pathList.begin(), pathList.end());

    for (const auto& singlePath : pathList) {
        std::ifstream binaryFile(singlePath, std::ios::binary | std::ios::ate);
        if (!binaryFile.is_open()) continue;

        size_t fileSize = static_cast<size_t>(binaryFile.tellg());
        binaryFile.seekg(0, std::ios::beg);

        // 硬件级 4096 字节页对齐内存分配
        char* rawPtr = static_cast<char*>(_aligned_malloc(fileSize, 4096));
        if (!rawPtr) continue;

        binaryFile.read(rawPtr, fileSize);

        AlignedBuffer buf;
        buf.data = std::unique_ptr<char[], AlignedFreeDeleter>(rawPtr);
        buf.size = fileSize;
        fileBufferList.push_back(std::move(buf));
    }

    return !fileBufferList.empty();
}

void FileInput::readFileToQueue(std::string folderPath, unsigned loopCount, double delayMs) {
    std::vector<AlignedBuffer> framePool;

    if (!loadFilesFromDisk(folderPath, framePool)) {
        std::cerr << "[FileInput] 无法从指定目录载入测试源文件，任务中止\n";
        isRunning = false;
        return;
    }

    const unsigned kInfiniteLoopFlag = 0;
    unsigned currentLoopIndex = 0;
    double delayMicroseconds = delayMs * 1000.0;

    while (isRunning.load() && (loopCount == kInfiniteLoopFlag || currentLoopIndex < loopCount)) {
        currentLoopIndex++;
        for (size_t i = 0; i < framePool.size(); ++i) {
            if (!isRunning.load()) break;

            FramePacketRef pktRef;
            pktRef.dataPtr = framePool[i].data.get();
            pktRef.size = framePool[i].size;

            outQueue.pushData(pktRef, isRunning);

            if (delayMicroseconds > 0.0) {
                hybridPreciseDelay(delayMicroseconds, isRunning);
            }
        }
    }
    isRunning = false;
}