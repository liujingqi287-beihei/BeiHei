#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include "safe_queue.h"
#include "board_pull.h"

class FileOutput {
public:
    FileOutput(const std::string& targetFilePath, SafeQueue<PacketBlockPtr>& sourceQueue);
    ~FileOutput();

    void startWrite();
    void stopWrite();

private:
    void writeDataToFile();
    void prepareTargetDirectory(const std::string& targetFilePath);

    std::string saveFilePath;
    std::ofstream outputFile;
    SafeQueue<PacketBlockPtr>& inQueue;
    std::atomic<bool> isRunning;
    std::thread workerThread;
};