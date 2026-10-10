#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "safe_queue.h"

struct AlignedFreeDeleter {
    void operator()(char* ptr) const {
        if (ptr) _aligned_free(ptr);
    }
};

struct AlignedBuffer {
    std::unique_ptr<char[], AlignedFreeDeleter> data;
    size_t size = 0;
};

struct FramePacketRef {
    const char* dataPtr = nullptr;
    size_t size = 0;
};

class FileInput {
public:
    explicit FileInput(SafeQueue<FramePacketRef>& targetQueue);
    ~FileInput();

    void startRead(const std::string& folderPath, unsigned loopCount = 0, double delayMs = 0.0);
    void stopRead();
    void waitForCompletion();

private:
    void readFileToQueue(std::string folderPath, unsigned loopCount, double delayMs);
    bool loadFilesFromDisk(const std::string& folderPath, std::vector<AlignedBuffer>& fileBufferList);

    SafeQueue<FramePacketRef>& outQueue;
    std::atomic<bool> isRunning;
    std::thread workerThread;
};