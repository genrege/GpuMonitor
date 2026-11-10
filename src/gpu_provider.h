#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "process_list.h"

struct GpuDeviceInfo
{
    std::wstring name;
    std::wstring driverMode;
    bool driverModeValid = false;
};

// One instantaneous reading; each metric is valid only if the vendor API could supply it.
struct GpuReading
{
    unsigned int load = 0;
    bool loadValid = false;
    unsigned int temperature = 0;
    bool temperatureValid = false;
    unsigned long long memoryUsed = 0;
    unsigned long long memoryTotal = 0;
    bool memoryValid = false;
};

// Vendor-neutral source of GPU metrics. A provider owns its API session and releases it on destruction.
class IGpuProvider
{
public:
    virtual ~IGpuProvider() = default;

    // Returns false with a user-facing message when the vendor API is unavailable.
    virtual bool Initialize(std::wstring& error) = 0;
    // Device order is stable; Read() takes an index into this list.
    virtual std::vector<GpuDeviceInfo> Enumerate() = 0;
    virtual GpuReading Read(size_t deviceIndex) = 0;
    // Processes currently using the device; memory is invalid where the driver does not report it.
    virtual std::vector<ProcessRow> ReadProcesses(size_t) { return {}; }
};

// Every provider that may be present on this machine; unusable ones fail in Initialize().
std::vector<std::unique_ptr<IGpuProvider>> CreateGpuProviders();
