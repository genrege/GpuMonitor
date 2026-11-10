#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "config.h"
#include "process_list.h"

struct Sample
{
    unsigned int load = 0;
    unsigned long long memoryUsed = 0;
    unsigned long long memoryTotal = 0;
    bool valid = false;
    bool memoryValid = false;
    ULONGLONG timestampMs = 0;
};

struct SystemSample
{
    unsigned int cpuPercent = 0;
    unsigned int ramPercent = 0;
    unsigned long long ramUsed = 0;
    unsigned long long ramTotal = 0;
    bool cpuValid = false;
    bool ramValid = false;
    ULONGLONG timestampMs = 0;
};

struct SystemInfo
{
    std::vector<SystemSample> history = std::vector<SystemSample>(kHistoryCapacity);
    int historyNext = 0;
    int historyCount = 0;
    bool hasPreviousTimes = false;
    ULONGLONG previousIdle = 0;
    ULONGLONG previousTotal = 0;
    std::vector<ProcessRow> processes;
    ProcessCpuTracker cpuTracker;
};

class IGpuProvider;

struct GpuInfo
{
    IGpuProvider* provider = nullptr;
    size_t deviceIndex = 0;
    std::wstring name;
    std::wstring driverMode;
    unsigned int load = 0;
    unsigned int temperature = 0;
    unsigned long long memoryTotal = 0;
    bool loadValid = false;
    bool temperatureValid = false;
    bool driverModeValid = false;
    std::vector<Sample> history = std::vector<Sample>(kHistoryCapacity);
    int historyNext = 0;
    int historyCount = 0;
    std::vector<ProcessRow> processes;
};

// Latest-first access into a ring-buffer history.
template <typename T>
const T* HistoryAt(const std::vector<T>& history, int next, int count, int offset)
{
    if (offset >= count)
    {
        return nullptr;
    }
    return &history[(next - 1 - offset + kHistoryCapacity) % kHistoryCapacity];
}

