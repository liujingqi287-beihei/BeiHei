//
// XDMA 设备管理API
// 为保证跨平台兼容性，只能使用标准C/C++库函数实现，不允许使用第三方库
//
#include <string>
#include "xdma_manager.h"
#include <windows.h>

const std::string boardDumpPrefix = "../boardDump";

XdmaManager::XdmaManager()
{
}

XdmaManager::~XdmaManager()
{
    for (auto& pair : boardIndexToBoardInfoMap) 
    {
            cleanupDevice(pair.second);
    }
    boardIndexToBoardInfoMap.clear();
}

unsigned XdmaManager::scanBoard()
{
#ifdef NO_PCIE_BOARD
    return 16;
#else
    ScanBoardInfo scanBoardInfo;

    GUID guid = GUID_DEVINTERFACE_XDMA;
    HDEVINFO deviceInfo = SetupDiGetClassDevs((LPGUID)&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "GetDevices INVALID_HANDLE_VALUE\n");
        exit(0);
    }

    SP_DEVICE_INTERFACE_DATA deviceInterface;
    deviceInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // enumerate through devices
    DWORD index;
    for (index = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &guid, index, &deviceInterface); ++index) {

        // get required buffer size
        ULONG detailLength = 0;
        if (!SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterface, NULL, 0, &detailLength, NULL) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get length failed\n");
            break;
        }

        // allocate space for device interface detail
        PSP_DEVICE_INTERFACE_DETAIL_DATA devDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
        if (!devDetail) {
            fprintf(stderr, "HeapAlloc failed\n");
            break;
        }
        devDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // get device interface detail
        if (!SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterface, devDetail, detailLength, NULL, NULL)) {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get detail failed\n");
            HeapFree(GetProcessHeap(), 0, devDetail);
            break;
        }
        
        StringCchCopy(scanBoardInfo.devPath[index], scanBoardInfo.devPathLen, devDetail->DevicePath);
        HeapFree(GetProcessHeap(), 0, devDetail);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);

    scanBoardInfo.devNum = index;

    scanBoardId(scanBoardInfo);
    return index;
#endif
}
bool XdmaManager::writePcieBarRegister(unsigned boardIndex, unsigned addr, unsigned value)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    (void)addr;
    (void)value;
    return true;
#else
    char devPath[MAX_PATH + 1] = "";
    strcat_s(devPath, sizeof devPath, boardIndexToBoardInfoMap[boardIndex].devPath);
    strcat_s(devPath, sizeof devPath, "\\");
    strcat_s(devPath, sizeof devPath, "user");

    HANDLE device = CreateFile(devPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        return false;
    }

    LARGE_INTEGER address;
    address.QuadPart = addr;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        CloseHandle(device);
        return false;
    }

    DWORD writeSize = 4;
    if (!WriteFile(device, &value, writeSize, &writeSize, NULL)) {
        fprintf(stderr, "WriteFile to device %s failed with Win32 error code: %ld\n",
            devPath, GetLastError());
        CloseHandle(device);
        return false;
    }

    CloseHandle(device);
    return true;
#endif
}
bool XdmaManager::readPcieBarRegister(char* devPathBase, unsigned addr, unsigned& value)
{
#ifdef NO_PCIE_BOARD
    (void)devPathBase;
    (void)addr;
    (void)value;
    return true;
#else
    char devPath[MAX_PATH + 1] = "";
    strcat_s(devPath, sizeof devPath, devPathBase);
    strcat_s(devPath, sizeof devPath, "\\");
    strcat_s(devPath, sizeof devPath, "user");

    HANDLE device = CreateFile(devPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        return false;
    }

    LARGE_INTEGER address;
    address.QuadPart = addr;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        CloseHandle(device);
        return false;
    }

    DWORD readSize = 4;
    if (!ReadFile(device, &value, readSize, &readSize, NULL)) {
        fprintf(stderr, "ReadFile from device %s failed with Win32 error code: %ld\n",
            devPath, GetLastError());
        CloseHandle(device);
        return false;
    }

    CloseHandle(device);
    return true;
#endif
}
void XdmaManager::scanBoardId(ScanBoardInfo& deviceInfo)
{    
    for (int i = 0; i < deviceInfo.devNum; ++i)
    {
        unsigned boardIndex;
        readPcieBarRegister(deviceInfo.devPath[i], kPcieBarRegStatus, boardIndex);
        boardIndex &= 0xF;

        scanIndexToBoardIdMap[i] = boardIndex;

        strcpy_s(boardIndexToBoardInfoMap[boardIndex].devPath, sizeof(boardIndexToBoardInfoMap[boardIndex].devPath), deviceInfo.devPath[i]);
        boardIndexToBoardInfoMap[boardIndex].scanIndex = i;
    }
}

int XdmaManager::read(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned sizeOfBytes, unsigned offset)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    (void)channelIndex;
    (void)pBuff;
    (void)offset;
    return sizeOfBytes;
#else

    LARGE_INTEGER address;
    address.QuadPart = offset;

    HANDLE device = getReadHandle(boardIndex, channelIndex);

    // set file pointer to offset of target address within PCIe BAR
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -1;
    }

    DWORD readSize = 0;
    if (!ReadFile(device, pBuff, sizeOfBytes, &readSize, NULL)) {
        //fprintf(stderr, "ReadFile from device %s failed with Win32 error code: %ld\n",, GetLastError());
        return -1;
    }

    return readSize;
#endif
}

int XdmaManager::write(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned sizeOfBytes, unsigned offset)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    (void)channelIndex;
    (void)pBuff;
    (void)offset;
    return sizeOfBytes;
#else
    LARGE_INTEGER address;
    address.QuadPart = offset;

    HANDLE device = getWriteHandle(boardIndex, channelIndex);

    // set file pointer to offset of target address within PCIe BAR
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -1;
    }

    DWORD writeSize = 0;
    //QueryPerformanceCounter(&start);
    if (!WriteFile(device, pBuff, sizeOfBytes, &writeSize, NULL)) {
        //fprintf(stderr, "WriteFile to device %s failed with Win32 error code: %d\n",devPath, GetLastError());
        return -1;
    }


    return writeSize;
#endif
}

int XdmaManager::read_stream(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned sizeOfBytes)
{
#ifdef NO_PCIE_BOARD
    std::string boardDumpFilePath = boardDumpPrefix;
    boardDumpFilePath += std::to_string(boardIndex);
    boardDumpFilePath += std::to_string(channelIndex);
    boardDumpFilePath += ".dat";
    FILE* outFile = fopen(boardDumpFilePath.c_str(), "rb");

    _fseeki64(outFile, 0, SEEK_END);
    _int64 fileLen = _ftelli64(outFile);

    while (fileLen < sizeOfBytes + totalReadSize[boardIndex])
    {
        Sleep(50);
        _fseeki64(outFile, 0, SEEK_END);
        fileLen = _ftelli64(outFile);
    }
    _fseeki64(outFile, totalReadSize[boardIndex], SEEK_SET);
    size_t readSize = fread(pBuff, 1, sizeOfBytes, outFile);
    if (readSize != sizeOfBytes)
    {
        printf("读错误\n");
    }
    totalReadSize[boardIndex] += sizeOfBytes;
    fclose(outFile);
    return sizeOfBytes;
#else
    DWORD readSize = 0;
    HANDLE device = getReadHandle(boardIndex, channelIndex);

    if (!ReadFile(device, pBuff, sizeOfBytes, &readSize, NULL)) {
        //fprintf(stderr, "ReadFile from device %s failed with Win32 error code: %ld\n", GetLastError());
        return -1;
    }

    return readSize;
#endif
}

int XdmaManager::write_stream(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned sizeOfBytes)
{
#ifdef NO_PCIE_BOARD
    std::string boardDumpFilePath = boardDumpPrefix;
    boardDumpFilePath += std::to_string(boardIndex);
    boardDumpFilePath += std::to_string(channelIndex);
    boardDumpFilePath += ".dat";
    FILE* outFile = fopen(boardDumpFilePath.c_str(), "ab+");
    fwrite(pBuff, 1, sizeOfBytes, outFile);
    fflush(outFile);
    fclose(outFile);
    return sizeOfBytes;
#else
    DWORD writeSize = 0;
    HANDLE device = getWriteHandle(boardIndex, channelIndex);

    if (!WriteFile(device, pBuff, sizeOfBytes, &writeSize, NULL)) {
        //fprintf(stderr, "WriteFile to device %s failed with Win32 error code: %d\n",devPath, GetLastError());
        return -1;
    }

    return writeSize;
#endif
}

bool XdmaManager::resetBoard(unsigned boardIndex)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    return true;
#else
    return writePcieBarRegister(boardIndex, kPcieBarRegControl, 0x00FF0001);
#endif
}

BoardInfo* XdmaManager::getBoardInfo(unsigned boardIndex)
{
    auto it = boardIndexToBoardInfoMap.find(boardIndex);
    if (it != boardIndexToBoardInfoMap.end()) {
        return &(it->second);
    }
    return nullptr;
}

bool XdmaManager::isBoardOpened(unsigned boardIndex)
{
    return boardIndexToBoardInfoMap.find(boardIndex) != boardIndexToBoardInfoMap.end();
}

HANDLE XdmaManager::getReadHandle(unsigned boardIndex, unsigned channelIndex)
{
    BoardInfo* device = getBoardInfo(boardIndex);
    if (device && device->readHandle[channelIndex]!= INVALID_HANDLE_VALUE) {
        return device->readHandle[channelIndex];
    }
    return INVALID_HANDLE_VALUE;
}

HANDLE XdmaManager::getWriteHandle(unsigned boardIndex, unsigned channelIndex)
{
    BoardInfo* device = getBoardInfo(boardIndex);
    if (device && device->writeHandle[channelIndex] != INVALID_HANDLE_VALUE) {
        return device->writeHandle[channelIndex];
    }
    return INVALID_HANDLE_VALUE;
}
bool XdmaManager::openChannel(unsigned boardIndex, unsigned channelIndex, bool isSendFlag)
{
#ifdef NO_PCIE_BOARD
    (void)isSendFlag;
    totalReadSize[boardIndex] = 0;
    std::string boardDumpFilePath = boardDumpPrefix;
    boardDumpFilePath += std::to_string(boardIndex);
    boardDumpFilePath += std::to_string(channelIndex);
    boardDumpFilePath += ".dat";

    FILE* outFile = fopen(boardDumpFilePath.c_str(), "wb");
    fclose(outFile);
    return true;
#else
    BoardInfo* device = getBoardInfo(boardIndex);
    if (!device) {
        return false;
    }

    char devPath[MAX_PATH + 1] = "";

    if (isSendFlag)
    {
        if (device->writeHandle[channelIndex] != INVALID_HANDLE_VALUE)
        {
            return true;
        }
        sprintf(devPath, "%s\\h2c_%d", device->devPath, channelIndex);
        device->writeHandle[channelIndex] = CreateFile(devPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    else
    {
        if (device->readHandle[channelIndex] != INVALID_HANDLE_VALUE)
        {
            return true;
        }
        sprintf(devPath, "%s\\c2h_%d", device->devPath, channelIndex);
        device->readHandle[channelIndex] = CreateFile(devPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    return true;

#endif
}

bool XdmaManager::openReadChannel(unsigned boardIndex, unsigned channelIndex)
{
    return openChannel(boardIndex, channelIndex, false);
}

bool XdmaManager::openWriteChannel(unsigned boardIndex, unsigned channelIndex)
{
    return openChannel(boardIndex, channelIndex, true);
}
bool XdmaManager::closeChannel(unsigned boardIndex, unsigned channelIndex, bool isSendFlag)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    (void)channelIndex;
    (void)isSendFlag;
    return true;
#else
    BoardInfo* device = getBoardInfo(boardIndex);
    if (!device) {
        return false;
    }

    char devPath[MAX_PATH + 1] = "";

    if (isSendFlag)
    {
        if (device->writeHandle[channelIndex] != INVALID_HANDLE_VALUE)
        {
            CloseHandle(device->writeHandle[channelIndex]);
            device->writeHandle[channelIndex] = INVALID_HANDLE_VALUE;
            return true;
        }
    }
    else
    {
        if (device->readHandle[channelIndex] != INVALID_HANDLE_VALUE)
        {
            CloseHandle(device->writeHandle[channelIndex]);
            device->writeHandle[channelIndex] = INVALID_HANDLE_VALUE;
            return true;
        }
    }

    return true;

#endif
}
bool XdmaManager::closeReadChannel(unsigned boardIndex, unsigned channelIndex)
{
    return closeChannel(boardIndex, channelIndex, false);
}

bool XdmaManager::closeWriteChannel(unsigned boardIndex, unsigned channelIndex)
{
    return closeChannel(boardIndex, channelIndex, true);
}

bool XdmaManager::closeBoard(unsigned boardIndex)
{
#ifdef NO_PCIE_BOARD
    (void)boardIndex;
    return true;
#else
    BoardInfo* device = getBoardInfo(boardIndex);
    if (!device) {
        return false;
    }

    cleanupDevice(*device);
    return true;

#endif
}

void XdmaManager::cleanupDevice(BoardInfo& device)
{
    for (unsigned i = 0; i < kMaxChannel; i++)
    {
        if (device.writeHandle[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(device.writeHandle[i]);
            device.writeHandle[i] = INVALID_HANDLE_VALUE;
        }
        if (device.readHandle[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(device.readHandle[i]);
            device.readHandle[i] = INVALID_HANDLE_VALUE;
        }
    }

    if (device.userHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(device.userHandle);
        device.userHandle = INVALID_HANDLE_VALUE;
    }
}