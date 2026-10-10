#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <filesystem>
#include <windows.h>

#include "xdma_manager.h"
#include "safe_queue.h"
#include "file_input.h"
#include "board_push.h"
#include "board_pull.h"
#include "file_output.h"

namespace fs = std::filesystem;

struct AppConfig {
    unsigned boardIndex = 0;
    unsigned channelIndex = 0;
    std::string inputFolderPath = "C:/ljq/CS/xl";
    std::string outputRootDir = "C:/ljq/CS/cs";
    std::string outputFileName = "stream_all_received.dat";
    unsigned loopCount = 0;
    double delayMs = 1.0;
    unsigned dmaChunkSize = 225280;
    size_t queueCapacity = 1000;

    unsigned short netPort = 55555;
    bool enableNetOutput = false;
    bool enableFileOutput = true;
};

static std::string buildCurrentTimestampString() {
    auto nowTimePoint = std::chrono::system_clock::now();
    std::time_t currentTimer = std::chrono::system_clock::to_time_t(nowTimePoint);
    std::tm localTm;
    localtime_s(&localTm, &currentTimer);
    std::ostringstream stringStream;
    stringStream << std::put_time(&localTm, "%Y年%m月%d日%H时%M分%S秒");
    return stringStream.str();
}

static std::string trimJsonValue(const std::string& inputStr) {
    size_t first = inputStr.find_first_not_of(" \t\r\n\",");
    if (first == std::string::npos) return "";
    size_t last = inputStr.find_last_not_of(" \t\r\n\",");
    return inputStr.substr(first, (last - first + 1));
}

std::string resolveConfigPath(const std::string& customPath) {
    if (!customPath.empty() && fs::exists(customPath)) return customPath;
    const std::string candidatePaths[] = { "config.json", "../config.json", "../../config.json" };
    for (const auto& path : candidatePaths) {
        if (fs::exists(path)) return path;
    }
    return "config.json";
}

bool loadConfigFromJson(const std::string& jsonFilePath, AppConfig& targetConfig) {
    if (!fs::exists(jsonFilePath)) return false;
    std::ifstream jsonFile(jsonFilePath);
    if (!jsonFile.is_open()) return false;

    std::string lineStr;
    while (std::getline(jsonFile, lineStr)) {
        size_t colonPos = lineStr.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = trimJsonValue(lineStr.substr(0, colonPos));
        std::string value = trimJsonValue(lineStr.substr(colonPos + 1));
        if (key.empty() || value.empty()) continue;

        if (key == "boardIndex") targetConfig.boardIndex = static_cast<unsigned>(std::stoul(value));
        else if (key == "channelIndex") targetConfig.channelIndex = static_cast<unsigned>(std::stoul(value));
        else if (key == "inputFolderPath") targetConfig.inputFolderPath = value;
        else if (key == "outputRootDir" || key == "recvSaveFilePath") targetConfig.outputRootDir = value;
        else if (key == "outputFileName") targetConfig.outputFileName = value;
        else if (key == "loopCount") targetConfig.loopCount = static_cast<unsigned>(std::stoul(value));
        else if (key == "delayMs") targetConfig.delayMs = std::stod(value);
        else if (key == "dmaChunkSize") targetConfig.dmaChunkSize = static_cast<unsigned>(std::stoul(value));
        else if (key == "queueCapacity") targetConfig.queueCapacity = static_cast<size_t>(std::stoull(value));
        else if (key == "enableFileOutput") targetConfig.enableFileOutput = (value == "true" || value == "1");
    }
    return true;
}

void runSpeedMonitorProcess(const BoardPush& boardPushRef,
    const BoardPull& boardPullRef,
    const SafeQueue<FramePacketRef>& toBoardQueue,
    const SafeQueue<PacketBlockPtr>* fileQueuePtr,
    const std::atomic<bool>& isRunning)
{
    const unsigned kMonitorIntervalMs = 1000;
    const double kBitsPerByte = 8.0;
    const double kGigabitDivisor = 1000.0 * 1000.0 * 1000.0;
    const double kMegaDivisor = 1024.0 * 1024.0;

    LARGE_INTEGER timerFrequency, lastTime, currentTime;
    QueryPerformanceFrequency(&timerFrequency);
    QueryPerformanceCounter(&lastTime);

    uint64_t previousSentBytes = 0;
    uint64_t previousRecvBytes = 0;

    while (isRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kMonitorIntervalMs));

        QueryPerformanceCounter(&currentTime);
        double elapsedSeconds = static_cast<double>(currentTime.QuadPart - lastTime.QuadPart) / timerFrequency.QuadPart;
        lastTime = currentTime;
        if (elapsedSeconds <= 0.0) continue;

        uint64_t currentSentBytes = boardPushRef.getTotalSentBytes();
        uint64_t currentRecvBytes = boardPullRef.getTotalPulledBytes();
        uint64_t pushDropped = boardPushRef.getDroppedFrameCount();
        uint64_t sentFrames = boardPushRef.getSentFrameCount();
        uint64_t recvFrames = boardPullRef.getTotalPulledFrames();

        uint64_t deltaSendBytes = currentSentBytes - previousSentBytes;
        uint64_t deltaRecvBytes = currentRecvBytes - previousRecvBytes;
        previousSentBytes = currentSentBytes;
        previousRecvBytes = currentRecvBytes;

        double avgSendSpeedGbps = (static_cast<double>(deltaSendBytes) * kBitsPerByte) / (elapsedSeconds * kGigabitDivisor);
        double avgRecvSpeedGbps = (static_cast<double>(deltaRecvBytes) * kBitsPerByte) / (elapsedSeconds * kGigabitDivisor);

        size_t sendBufCount = toBoardQueue.getQueueSize();
        size_t fileBufCount = fileQueuePtr ? fileQueuePtr->getQueueSize() : 0;

        /*int64_t droppedFrames = static_cast<int64_t>(sentFrames) - static_cast<int64_t>(recvFrames);
        if (droppedFrames < 0) droppedFrames = 0;*/
        int64_t diffBytes = static_cast<int64_t>(currentSentBytes) - static_cast<int64_t>(currentRecvBytes);

        std::cout << "\r[XDMA]速率: 发 " << std::fixed << std::setprecision(2) << avgSendSpeedGbps << "G / 收 " << avgRecvSpeedGbps << "G | "
            << "累计(发/收): " << std::fixed << std::setprecision(1) << (currentSentBytes / kMegaDivisor) << "MB / " << (currentRecvBytes / kMegaDivisor) << "MB | "
            << "内存堆积: " << sendBufCount << "/" << fileBufCount << " | "
            << "板卡吞包量: " << diffBytes << " B | "
            << "发送丢包: " << pushDropped << " 帧" << std::flush;
    }
}

static bool performRobustBoardRest(unsigned boardIndex) {
    auto& xdma = XdmaManager::getInstance();
    const unsigned kPcieBarRegControl = 0x00000040;
    const unsigned kPcieBarRegStartRecv = 0x00000100;
    if (!xdma.writePcieBarRegister(boardIndex, kPcieBarRegStartRecv, 0))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!xdma.writePcieBarRegister(boardIndex, kPcieBarRegControl, 0x00FF0001))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (!xdma.writePcieBarRegister(boardIndex, kPcieBarRegControl, 0x00000000))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    unsigned statusVal = 0;
    if (xdma.readPcieBarRegister(boardIndex, 0x0, statusVal))
    {

    }
    return true;
}

int main(int argc, char* argv[])
{
    AppConfig config;
    std::string customPath = (argc > 1) ? argv[1] : "";
    std::string configPath = resolveConfigPath(customPath);
    loadConfigFromJson(configPath, config);

    std::string timestampDirName = buildCurrentTimestampString();
    fs::path finalOutputFolderPath = fs::path(config.outputRootDir) / timestampDirName;
    fs::path finalSaveFilePath = finalOutputFolderPath / config.outputFileName;
    fs::path mirrorSentSaveFilePath = finalOutputFolderPath / "mirror_sent.src";

    std::cout << "====================================================\n";
    std::cout << "   XDMA 高性能基准压测套件                      \n";
    std::cout << "   - 输入源目录: " << config.inputFolderPath << "\n";
    std::cout << "   - 循环发送轮数: " << (config.loopCount == 0 ? "无限循环" : std::to_string(config.loopCount) + " 轮") << "\n";
    std::cout << "   - 帧发送间隔: " << config.delayMs << " ms\n";
    std::cout << "   - 帧单次尺寸: " << config.dmaChunkSize << " Bytes\n";
    std::cout << "   - 接收输出文件: " << finalSaveFilePath.string() << "\n";
    std::cout << "====================================================\n";

    int boardCount = XdmaManager::getInstance().scanBoard();
    if (boardCount <= 0) {
        std::cerr << "未找到可用 XDMA 板卡，程序退出。\n";
        return -1;
    }

    if (!performRobustBoardRest(config.boardIndex)) {
        std::cerr << "复位失败\n";
    XdmaManager::getInstance().closeBoard(config.boardIndex);
    return -1;
    }

    SafeQueue<FramePacketRef> toBoardQueue(config.queueCapacity);
    std::unique_ptr<SafeQueue<PacketBlockPtr>> toFileQueue = nullptr;

    if (config.enableFileOutput) {
        toFileQueue = std::make_unique<SafeQueue<PacketBlockPtr>>(config.queueCapacity);
    }

    FileInput fileInputTask(toBoardQueue);
    BoardPush boardPushTask(config.boardIndex, config.channelIndex, toBoardQueue, mirrorSentSaveFilePath.string());
    BoardPull boardPullTask(config.boardIndex, config.channelIndex,
        toFileQueue ? toFileQueue.get() : nullptr,
        nullptr,
        config.dmaChunkSize);

    std::unique_ptr<FileOutput> fileOutputTask = nullptr;
    if (config.enableFileOutput) {
        fileOutputTask = std::make_unique<FileOutput>(finalSaveFilePath.string(), *toFileQueue);
        fileOutputTask->startWrite();
    }

    boardPushTask.startWrite();
    fileInputTask.startRead(config.inputFolderPath, config.loopCount, config.delayMs);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    boardPullTask.startRead();

    std::atomic<bool> isMonitorRunning(true);
    std::thread monitorThread(&runSpeedMonitorProcess,
        std::cref(boardPushTask),
        std::cref(boardPullTask),
        std::cref(toBoardQueue),
        toFileQueue.get(),
        std::ref(isMonitorRunning));

    fileInputTask.waitForCompletion();

    while (toBoardQueue.getQueueSize() > 0 || (toFileQueue && toFileQueue->getQueueSize() > 0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    isMonitorRunning = false;
    if (monitorThread.joinable()) monitorThread.join();

    fileInputTask.stopRead();
    boardPushTask.stopWrite();
    boardPullTask.stopRead();

    if (fileOutputTask) fileOutputTask->stopWrite();

    XdmaManager::getInstance().writePcieBarRegister(config.boardIndex, 0x00000100, 0);
    XdmaManager::getInstance().closeBoard(config.boardIndex);
    std::cout << "\n\n>>> 传输任务安全终止，全部资源已释放。\n";
    return 0;
}
