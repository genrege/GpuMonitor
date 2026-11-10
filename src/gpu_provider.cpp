#include "gpu_provider.h"

#include "nvml_provider.h"

std::vector<std::unique_ptr<IGpuProvider>> CreateGpuProviders()
{
    std::vector<std::unique_ptr<IGpuProvider>> providers;
    providers.push_back(std::make_unique<NvmlProvider>());
    return providers;
}
