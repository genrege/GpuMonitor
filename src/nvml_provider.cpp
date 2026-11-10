#include "nvml_provider.h"

#include <windows.h>

#include <algorithm>

#include "format.h"

namespace
{

std::wstring NvmlError(const char* operation, nvmlReturn_t result)
{
    std::wstring message = ToWide(operation);
    message += L": ";
    message += ToWide(nvmlErrorString(result));
    return message;
}

std::wstring GetDriverMode(nvmlDevice_t device, bool& valid)
{
    nvmlDriverModel_t current{};
    nvmlDriverModel_t pending{};
    valid = nvmlDeviceGetDriverModel(device, &current, &pending) == NVML_SUCCESS;
    if (!valid)
    {
        return L"MODE N/A";
    }

    switch (current)
    {
        case NVML_DRIVER_WDM:
            return L"TCC";
        case NVML_DRIVER_WDDM:
            return L"WDDM";
        case NVML_DRIVER_MCDM:
            return L"MCDM";
        default:
            return L"UNKNOWN";
    }
}

} // namespace

NvmlProvider::~NvmlProvider()
{
    if (initialized_)
    {
        nvmlShutdown();
    }
}

bool NvmlProvider::Initialize(std::wstring& error)
{
    const nvmlReturn_t result = nvmlInit_v2();
    if (result != NVML_SUCCESS)
    {
        error = NvmlError("NVML initialization failed", result);
        return false;
    }
    initialized_ = true;
    return true;
}

std::vector<GpuDeviceInfo> NvmlProvider::Enumerate()
{
    std::vector<GpuDeviceInfo> infos;
    unsigned int deviceCount = 0;
    if (nvmlDeviceGetCount_v2(&deviceCount) != NVML_SUCCESS)
    {
        return infos;
    }

    for (unsigned int index = 0; index < deviceCount; ++index)
    {
        nvmlDevice_t device = nullptr;
        if (nvmlDeviceGetHandleByIndex_v2(index, &device) != NVML_SUCCESS)
        {
            continue;
        }

        char deviceName[NVML_DEVICE_NAME_BUFFER_SIZE]{};
        if (nvmlDeviceGetName(device, deviceName, sizeof(deviceName)) != NVML_SUCCESS)
        {
            wsprintfA(deviceName, "GPU %u", index);
        }

        GpuDeviceInfo info;
        info.name = ToWide(deviceName);
        info.driverMode = GetDriverMode(device, info.driverModeValid);
        infos.push_back(info);
        devices_.push_back(device);
        lastSeenTimestamps_.push_back(0);
    }
    return infos;
}

GpuReading NvmlProvider::Read(size_t deviceIndex)
{
    GpuReading reading;
    const nvmlDevice_t device = devices_[deviceIndex];

    nvmlUtilization_t utilization{};
    reading.loadValid = nvmlDeviceGetUtilizationRates(device, &utilization) == NVML_SUCCESS;
    if (reading.loadValid)
    {
        reading.load = utilization.gpu;
    }

    reading.temperatureValid = nvmlDeviceGetTemperature(
                                   device, NVML_TEMPERATURE_GPU, &reading.temperature) == NVML_SUCCESS;

    nvmlMemory_t memory{};
    reading.memoryValid = nvmlDeviceGetMemoryInfo(device, &memory) == NVML_SUCCESS && memory.total > 0;
    reading.memoryUsed = memory.used;
    reading.memoryTotal = memory.total;
    return reading;
}

std::vector<ProcessRow> NvmlProvider::ReadProcesses(size_t deviceIndex)
{
    const nvmlDevice_t device = devices_[deviceIndex];
    std::vector<ProcessRow> rows;

    const auto addRunning = [&](auto query)
    {
        unsigned int count = 0;
        // The count can grow between the sizing call and the fetch; retry on insufficient size.
        std::vector<nvmlProcessInfo_t> infos;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            infos.resize(count + 8);
            count = static_cast<unsigned int>(infos.size());
            const nvmlReturn_t result = query(device, &count, infos.data());
            if (result == NVML_SUCCESS)
            {
                infos.resize(count);
                break;
            }
            if (result != NVML_ERROR_INSUFFICIENT_SIZE)
            {
                infos.clear();
                break;
            }
        }
        for (const nvmlProcessInfo_t& info : infos)
        {
            auto found = std::find_if(rows.begin(), rows.end(),
                [&](const ProcessRow& row) { return row.pid == info.pid; });
            if (found == rows.end())
            {
                rows.emplace_back();
                found = rows.end() - 1;
                found->pid = info.pid;
            }
            if (info.usedGpuMemory != NVML_VALUE_NOT_AVAILABLE)
            {
                found->memory += info.usedGpuMemory;
                found->memoryValid = true;
            }
        }
    };
    addRunning(nvmlDeviceGetComputeRunningProcesses);
    addRunning(nvmlDeviceGetGraphicsRunningProcesses);

    unsigned int sampleCount = 0;
    std::vector<nvmlProcessUtilizationSample_t> samples;
    unsigned long long& lastSeen = lastSeenTimestamps_[deviceIndex];
    nvmlReturn_t result = nvmlDeviceGetProcessUtilization(device, nullptr, &sampleCount, lastSeen);
    if (result == NVML_ERROR_INSUFFICIENT_SIZE && sampleCount > 0)
    {
        samples.resize(sampleCount);
        result = nvmlDeviceGetProcessUtilization(device, samples.data(), &sampleCount, lastSeen);
        samples.resize(result == NVML_SUCCESS ? sampleCount : 0);
    }
    const bool utilizationAvailable = result == NVML_SUCCESS || result == NVML_ERROR_NOT_FOUND ||
                                      result == NVML_ERROR_INSUFFICIENT_SIZE;
    // Oldest first, so the newest sample for a pid is applied last.
    std::sort(samples.begin(), samples.end(),
        [](const nvmlProcessUtilizationSample_t& a, const nvmlProcessUtilizationSample_t& b)
        { return a.timeStamp < b.timeStamp; });
    for (const nvmlProcessUtilizationSample_t& sample : samples)
    {
        lastSeen = std::max(lastSeen, sample.timeStamp);
        auto found = std::find_if(rows.begin(), rows.end(),
            [&](const ProcessRow& row) { return row.pid == sample.pid; });
        if (found == rows.end())
        {
            rows.emplace_back();
            found = rows.end() - 1;
            found->pid = sample.pid;
        }
        found->load = std::min(sample.smUtil, 100U);
        found->loadValid = true;
    }
    for (ProcessRow& row : rows)
    {
        row.name = ProcessNameFromPid(row.pid);
        if (!row.loadValid && utilizationAvailable)
        {
            row.load = 0;
            row.loadValid = true;
        }
    }
    return rows;
}
