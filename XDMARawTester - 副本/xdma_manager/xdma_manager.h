#pragma once

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <strsafe.h>
#include <map>
#include <atomic>

#include <Windows.h>
#include <SetupAPI.h>
#include <INITGUID.H>
#include <WinIoCtl.h>

#include "xdma_public.h"


const unsigned kMaxDevicePath = 20;

/// <summary>
/// 板卡默认ID分配
/// </summary>
const unsigned kBoardIdBitStreamRecv = 0;

/// <summary>
/// 板卡BAR寄存器地址
/// </summary>
const unsigned kPcieBarRegStatus = 0x0;//bit3-0：板卡ID
const unsigned kPcieBarRegControl = 0x00000040;//bit31-16: 复位时间， bit0：置‘1’复位

/// <summary>
/// 板卡通道数
/// </summary>
const unsigned kMaxChannel = 4;

struct ScanBoardInfo 
{
	char devPath[kMaxDevicePath][MAX_PATH + 1];
	int devPathLen;
	int devNum;
	ScanBoardInfo() {
		devNum = 0;
		devPathLen = MAX_PATH + 1;
		memset(devPath, 0, sizeof(devPath));
	}
};

struct BoardInfo
{
	char devPath[MAX_PATH + 1];
	HANDLE readHandle[kMaxChannel];
	HANDLE writeHandle[kMaxChannel];
	HANDLE userHandle;
	//std::atomic<bool> isOpenFlag;
	std::atomic<int> refCount;
	int scanIndex;
	BoardInfo() : userHandle(INVALID_HANDLE_VALUE), refCount(0)
	{
		memset(devPath, 0, sizeof(devPath));
		for (unsigned i = 0; i < kMaxChannel; i++)
		{
			readHandle[i] = INVALID_HANDLE_VALUE;
			writeHandle[i] = INVALID_HANDLE_VALUE;
		}
	}
};


class XdmaManager
{
public:
	static XdmaManager& getInstance()
	{
		static XdmaManager xdmaManager;
		return xdmaManager;
	}

	unsigned scanBoard();
	bool openReadChannel(unsigned boardIndex, unsigned channelIndex);
	bool closeReadChannel(unsigned boardIndex, unsigned channelIndex);
	bool openWriteChannel(unsigned boardIndex, unsigned channelIndex);
	bool closeWriteChannel(unsigned boardIndex, unsigned channelIndex);
	bool closeBoard(unsigned boardIndex);

	int read(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned bytesToRead, unsigned offset);
	int write(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned bytesToWrite, unsigned offset);
	int read_stream(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned len);
	int write_stream(unsigned boardIndex, unsigned channelIndex, void* pBuff, unsigned len);

	bool resetBoard(unsigned boardIndex);
	bool isBoardOpened(unsigned boardIndex);
	size_t getBoardCount() const { return boardIndexToBoardInfoMap.size(); }
	unsigned getBoardIndex(unsigned scanIndex) { return scanIndexToBoardIdMap[scanIndex]; }
	bool writePcieBarRegister(unsigned boardIndex, unsigned addr, unsigned value);
	bool readPcieBarRegister(unsigned boardIndex, unsigned addr, unsigned& value) {
		return readPcieBarRegister(boardIndexToBoardInfoMap[boardIndex].devPath, addr, value);
	}



private:
	XdmaManager();
	~XdmaManager();

	HANDLE getReadHandle(unsigned boardIndex, unsigned channelIndex);
	HANDLE getWriteHandle(unsigned boardIndex, unsigned channelIndex);
	bool openChannel(unsigned boardIndex, unsigned channelIndex, bool isSendFlag);
	bool closeChannel(unsigned boardIndex, unsigned channelIndex, bool isSendFlag);
	//HANDLE getUserHandle(unsigned boardIndex);
	void cleanupDevice(BoardInfo& device);
	void scanBoardId(ScanBoardInfo& deviceInfo);
	BoardInfo* getBoardInfo(unsigned boardIndex);
	bool readPcieBarRegister(char* devPathBase, unsigned addr, unsigned& value);

	std::map<unsigned, BoardInfo> boardIndexToBoardInfoMap;

	std::map<unsigned, unsigned> scanIndexToBoardIdMap;

	long long totalReadSize[16];

};

