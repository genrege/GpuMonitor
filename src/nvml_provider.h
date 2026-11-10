#pragma once
#include <nvml.h>

#include "gpu_provider.h"

class NvmlProvider final : public IGpuProvider
{
public:
    ~NvmlProvider() override;

    bool Initialize(std::wstring& error) override;
    std::vector<GpuDeviceInfo> Enumerate() override;
    GpuReading Read(size_t deviceIndex) override;
    std::vector<ProcessRow> ReadProcesses(size_t deviceIndex) override;

private:
    bool initialized_ = false;
    std::vector<nvmlDevice_t> devices_;
    std::vector<unsigned long long> lastSeenTimestamps_;
};
