#include "sampling.h"

#include <algorithm>

#include "settings.h"

void AddSystemSample(SystemInfo& system, ULONGLONG timestampMs)
{
    SystemSample sample;
    sample.timestampMs = timestampMs;

    FILETIME idleTime{}, kernelTime{}, userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        const auto toValue = [](const FILETIME& time)
        {
            return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        const ULONGLONG idle = toValue(idleTime);
        // Kernel time already includes idle time.
        const ULONGLONG total = toValue(kernelTime) + toValue(userTime);
        if (system.hasPreviousTimes && total > system.previousTotal)
        {
            const ULONGLONG totalDelta = total - system.previousTotal;
            const ULONGLONG idleDelta = std::min(idle - system.previousIdle, totalDelta);
            sample.cpuPercent = static_cast<unsigned int>(
                (totalDelta - idleDelta) * 100 / totalDelta);
            sample.cpuValid = true;
        }
        system.hasPreviousTimes = true;
        system.previousIdle = idle;
        system.previousTotal = total;
    }

    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys > 0)
    {
        sample.ramTotal = memory.ullTotalPhys;
        sample.ramUsed = memory.ullTotalPhys - memory.ullAvailPhys;
        sample.ramPercent = static_cast<unsigned int>(sample.ramUsed * 100 / sample.ramTotal);
        sample.ramValid = true;
    }

    system.history[system.historyNext] = sample;
    system.historyNext = (system.historyNext + 1) % kHistoryCapacity;
    system.historyCount = std::min(system.historyCount + 1, kHistoryCapacity);
}

void AddSample(GpuInfo& gpu, ULONGLONG timestampMs)
{
    const GpuReading reading = gpu.provider->Read(gpu.deviceIndex);
    gpu.loadValid = reading.loadValid;
    if (reading.loadValid)
    {
        gpu.load = reading.load;
    }
    gpu.temperatureValid = reading.temperatureValid;
    gpu.temperature = reading.temperature;
    if (reading.memoryValid)
    {
        gpu.memoryTotal = reading.memoryTotal;
    }
    gpu.history[gpu.historyNext] = {
        gpu.load, reading.memoryUsed, reading.memoryTotal, gpu.loadValid, reading.memoryValid, timestampMs};
    gpu.historyNext = (gpu.historyNext + 1) % kHistoryCapacity;
    gpu.historyCount = std::min(gpu.historyCount + 1, kHistoryCapacity);
}
void RecordSelections(AppState& state, ULONGLONG timestampMs)
{
    for (size_t index = 0; index < state.selections.size(); ++index)
    {
        PanelSelection& selection = state.selections[index];
        if (!state.keepProcessHistory && !selection.active)
        {
            selection.histories.clear();
            continue;
        }
        std::vector<ProcessRow> merged;
        const std::vector<ProcessRow>* rows = &state.system.processes;
        if (index < state.gpus.size())
        {
            rows = &state.gpus[index].processes;
        }
        else if (index == state.gpus.size())
        {
            std::vector<const std::vector<ProcessRow>*> lists;
            for (const GpuInfo& gpu : state.gpus)
            {
                lists.push_back(&gpu.processes);
            }
            merged = MergeProcessRows(lists);
            rows = &merged;
        }

        // Drop history of terminated processes, and of unselected ones when not keeping all.
        for (auto it = selection.histories.begin(); it != selection.histories.end();)
        {
            const unsigned long pid = it->first;
            const bool present = std::any_of(rows->begin(), rows->end(),
                [&](const ProcessRow& row) { return row.pid == pid; });
            const bool wanted = state.keepProcessHistory || pid == selection.pid;
            it = present && wanted ? std::next(it) : selection.histories.erase(it);
        }

        for (const ProcessRow& row : *rows)
        {
            if (!state.keepProcessHistory && row.pid != selection.pid)
            {
                continue;
            }
            SelectedProcessSample sample;
            sample.timestampMs = timestampMs;
            sample.load = row.load;
            sample.loadValid = row.loadValid;
            sample.memory = row.memory;
            sample.memoryValid = row.memoryValid;
            std::vector<SelectedProcessSample>& history = selection.histories[row.pid];
            history.push_back(sample);
            // Trim in chunks so the front erase is not paid on every sample.
            if (history.size() > kMaxProcessHistory + kMaxProcessHistory / 8)
            {
                history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxProcessHistory));
            }
        }
    }
}

void Refresh(AppState& state)
{
    const ULONGLONG timestampMs = GetTickCount64();
    for (GpuInfo& gpu : state.gpus)
    {
        AddSample(gpu, timestampMs);
    }
    AddSystemSample(state.system, timestampMs);
    if (state.lastProcessRefreshMs == 0 || timestampMs - state.lastProcessRefreshMs >= kProcessRefreshMs)
    {
        state.lastProcessRefreshMs = timestampMs;
        for (GpuInfo& gpu : state.gpus)
        {
            gpu.processes = gpu.provider->ReadProcesses(gpu.deviceIndex);
        }
        state.system.processes = QuerySystemProcesses(state.system.cpuTracker, timestampMs);
        RecordSelections(state, timestampMs);
    }
}

void UpdateGpuStatus(AppState& state)
{
    state.status = std::to_wstring(state.gpus.size()) + L" GPU";
    if (state.gpus.size() != 1)
    {
        state.status += L"s";
    }
    state.status += L"  |  Update every " + SamplingIntervalLabel(state.sampleIntervalMs);
}

void InitializeProviders(AppState& state)
{
    state.providers = CreateGpuProviders();
    std::wstring firstError;
    for (const auto& provider : state.providers)
    {
        std::wstring error;
        if (!provider->Initialize(error))
        {
            if (firstError.empty())
            {
                firstError = error;
            }
            continue;
        }
        state.providersReady = true;

        const std::vector<GpuDeviceInfo> devices = provider->Enumerate();
        for (size_t index = 0; index < devices.size(); ++index)
        {
            GpuInfo gpu;
            gpu.provider = provider.get();
            gpu.deviceIndex = index;
            gpu.name = devices[index].name;
            gpu.driverMode = devices[index].driverMode;
            gpu.driverModeValid = devices[index].driverModeValid;
            state.gpus.push_back(gpu);
        }
    }

    if (state.gpus.empty())
    {
        state.status = firstError.empty() ? L"No GPUs found" : firstError;
        return;
    }

    UpdateGpuStatus(state);
    state.selections.assign(state.gpus.size() + 2, {});
    Refresh(state);
}
