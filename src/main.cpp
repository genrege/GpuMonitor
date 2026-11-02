#include <windows.h>
#include <nvml.h>
#include "resource.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace {

constexpr wchar_t kWindowClass[] = L"NvidiaInfoGpuMonitorWindow";
constexpr wchar_t kSettingsRegistryPath[] = L"Software\\NvidiaInfoGpuMonitor";
constexpr int kTimerId = 1;
constexpr int kGraphRefreshTimerId = 2;
constexpr int kSampleIntervalMs = 1000;
constexpr int kUpdateSpeedControlId = 102;
constexpr int kUpdateSpeedButtonId = 103;
constexpr int kWindowSizeControlId = 104;
constexpr int kWindowSizeButtonId = 105;
constexpr int kUpdateSpeedComboWidth = 120;
constexpr int kUpdateSpeedComboHeight = 28;
constexpr int kUpdateSpeedDropdownHeight = 440;
constexpr int kHeaderGroupOffset = 220;
constexpr int kHistoryCapacity = 172801;
constexpr int kHeaderHeight = 64;
constexpr int kCardHeight = 270;
constexpr int kCardGap = 12;
constexpr int kCombinedVramRowHeight = 92;

struct SamplingOption {
    const wchar_t* label;
    int intervalMs;
};

struct WindowSizeOption {
    const wchar_t* label;
    unsigned int seconds;
};

constexpr std::array<SamplingOption, 15> kSamplingOptions{{
    { L"0.1 s", 100 },
    {L"0.25 s", 250 },
    {L"0.5 s", 500},
    {L"1 s", 1000},
    {L"2 s", 2000},
    {L"5 s", 5000},
    {L"10 s", 10000},
    {L"30 s", 30000},
    {L"60 s", 60000},
    {L"120 s", 120000},
    {L"180 s", 180000},
    {L"300 s", 300000},
    {L"600 s", 600000},
    {L"1800 s", 1800000},
    {L"3600 s", 3600000},
}};

constexpr std::array<WindowSizeOption, 12> kWindowSizeOptions{{
    {L"1 minute", 60},
    {L"2 minutes", 120},
    {L"5 minutes", 300},
    {L"10 minutes", 600},
    {L"15 minutes", 900},
    {L"30 minutes", 1800},
    {L"1 hour", 3600},
    {L"2 hours", 7200},
    {L"4 hours", 14400},
    {L"8 hours", 28800},
    {L"12 hours", 43200},
    {L"24 hours", 86400},
}};

const COLORREF kBackground = RGB(19, 23, 27);
const COLORREF kCardBackground = RGB(29, 35, 39);
const COLORREF kPlotBackground = RGB(22, 28, 31);
const COLORREF kGridColor = RGB(53, 63, 67);
const COLORREF kPrimaryText = RGB(234, 240, 239);
const COLORREF kMutedText = RGB(145, 160, 161);
const COLORREF kAccent = RGB(65, 212, 169);
const COLORREF kDropdownBackground = RGB(5, 7, 9);
const COLORREF kDropdownText = RGB(84, 224, 237);
const COLORREF kDropdownBorder = RGB(68, 78, 82);
const COLORREF kMemory = RGB(91, 174, 245);
const COLORREF kTemperature = RGB(255, 174, 96);
const COLORREF kWarning = RGB(255, 129, 111);

struct Sample {
    unsigned int load = 0;
    unsigned long long memoryUsed = 0;
    unsigned long long memoryTotal = 0;
    bool valid = false;
    bool memoryValid = false;
    ULONGLONG timestampMs = 0;
};

struct PixelRange {
    unsigned long long minimum = std::numeric_limits<unsigned long long>::max();
    unsigned long long maximum = 0;
    bool hasValue = false;
};

struct GpuInfo {
    nvmlDevice_t device = nullptr;
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
};

struct Fonts {
    HFONT title = nullptr;
    HFONT heading = nullptr;
    HFONT body = nullptr;
    HFONT metric = nullptr;
};

struct AppState {
    std::vector<GpuInfo> gpus;
    std::wstring status;
    HWND updateSpeedCombo = nullptr;
    HWND updateSpeedButton = nullptr;
    HWND windowSizeCombo = nullptr;
    HWND windowSizeButton = nullptr;
    HBRUSH dropdownBackground = nullptr;
    HBRUSH dropdownBorder = nullptr;
    int sampleIntervalMs = kSampleIntervalMs;
    unsigned int graphWindowSeconds = 60;
    bool nvmlInitialized = false;
    int scrollPosition = 0;
    Fonts fonts;
};

void LoadSettings(AppState& state) {
    DWORD sampleIntervalMs = 0;
    DWORD valueSize = sizeof(sampleIntervalMs);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, L"SampleIntervalMs",
        RRF_RT_REG_DWORD, nullptr, &sampleIntervalMs, &valueSize) == ERROR_SUCCESS) {
        for (const SamplingOption& option : kSamplingOptions) {
            if (sampleIntervalMs == static_cast<DWORD>(option.intervalMs)) {
                state.sampleIntervalMs = option.intervalMs;
                break;
            }
        }
    }

    DWORD graphWindowSeconds = 0;
    valueSize = sizeof(graphWindowSeconds);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, L"GraphWindowSeconds",
        RRF_RT_REG_DWORD, nullptr, &graphWindowSeconds, &valueSize) == ERROR_SUCCESS) {
        for (const WindowSizeOption& option : kWindowSizeOptions) {
            if (graphWindowSeconds == option.seconds) {
                state.graphWindowSeconds = option.seconds;
                break;
            }
        }
    }
}

void SaveSettings(const AppState& state) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsRegistryPath, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    const DWORD sampleIntervalMs = static_cast<DWORD>(state.sampleIntervalMs);
    RegSetValueExW(key, L"SampleIntervalMs", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&sampleIntervalMs), sizeof(sampleIntervalMs));
    const DWORD graphWindowSeconds = state.graphWindowSeconds;
    RegSetValueExW(key, L"GraphWindowSeconds", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&graphWindowSeconds), sizeof(graphWindowSeconds));
    RegCloseKey(key);
}

LRESULT FindSamplingOption(const AppState& state) {
    for (size_t index = 0; index < kSamplingOptions.size(); ++index) {
        if (state.sampleIntervalMs == kSamplingOptions[index].intervalMs) {
            return static_cast<LRESULT>(index);
        }
    }
    return 0;
}

LRESULT FindWindowSizeOption(const AppState& state) {
    for (size_t index = 0; index < kWindowSizeOptions.size(); ++index) {
        if (state.graphWindowSeconds == kWindowSizeOptions[index].seconds) {
            return static_cast<LRESULT>(index);
        }
    }
    return 0;
}

std::wstring ToWide(const char* text) {
    if (text == nullptr || *text == '\0') {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

std::wstring NvmlError(const char* operation, nvmlReturn_t result) {
    std::wstring message = ToWide(operation);
    message += L": ";
    message += ToWide(nvmlErrorString(result));
    return message;
}

std::wstring GetDriverMode(nvmlDevice_t device, bool& valid) {
    nvmlDriverModel_t current{};
    nvmlDriverModel_t pending{};
    valid = nvmlDeviceGetDriverModel(device, &current, &pending) == NVML_SUCCESS;
    if (!valid) {
        return L"MODE N/A";
    }

    switch (current) {
    case NVML_DRIVER_WDM: return L"TCC";
    case NVML_DRIVER_WDDM: return L"WDDM";
    case NVML_DRIVER_MCDM: return L"MCDM";
    default: return L"UNKNOWN";
    }
}

void AddSample(GpuInfo& gpu, ULONGLONG timestampMs) {
    nvmlUtilization_t utilization{};
    gpu.loadValid = nvmlDeviceGetUtilizationRates(gpu.device, &utilization) == NVML_SUCCESS;
    if (gpu.loadValid) {
        gpu.load = utilization.gpu;
    }

    gpu.temperatureValid = nvmlDeviceGetTemperature(
        gpu.device, NVML_TEMPERATURE_GPU, &gpu.temperature) == NVML_SUCCESS;

    nvmlMemory_t memory{};
    const bool memoryValid = nvmlDeviceGetMemoryInfo(gpu.device, &memory) == NVML_SUCCESS
        && memory.total > 0;
    if (memoryValid) {
        gpu.memoryTotal = memory.total;
    }
    gpu.history[gpu.historyNext] = {
        gpu.load, memory.used, memory.total, gpu.loadValid, memoryValid, timestampMs};
    gpu.historyNext = (gpu.historyNext + 1) % kHistoryCapacity;
    gpu.historyCount = std::min(gpu.historyCount + 1, kHistoryCapacity);
}

void Refresh(AppState& state) {
    const ULONGLONG timestampMs = GetTickCount64();
    for (GpuInfo& gpu : state.gpus) {
        AddSample(gpu, timestampMs);
    }
}

std::wstring SamplingIntervalLabel(int intervalMs) {
    for (const SamplingOption& option : kSamplingOptions) {
        if (option.intervalMs == intervalMs) {
            return option.label;
        }
    }
    return L"2 s";
}

void UpdateGpuStatus(AppState& state) {
    state.status = std::to_wstring(state.gpus.size()) + L" NVIDIA GPU";
    if (state.gpus.size() != 1) {
        state.status += L"s";
    }
    state.status += L"  |  Update every " + SamplingIntervalLabel(state.sampleIntervalMs);
}

void InitializeNvml(AppState& state) {
    nvmlReturn_t result = nvmlInit_v2();
    if (result != NVML_SUCCESS) {
        state.status = NvmlError("NVML initialization failed", result);
        return;
    }
    state.nvmlInitialized = true;

    unsigned int deviceCount = 0;
    result = nvmlDeviceGetCount_v2(&deviceCount);
    if (result != NVML_SUCCESS) {
        state.status = NvmlError("Could not enumerate GPUs", result);
        return;
    }

    for (unsigned int index = 0; index < deviceCount; ++index) {
        nvmlDevice_t device = nullptr;
        result = nvmlDeviceGetHandleByIndex_v2(index, &device);
        if (result != NVML_SUCCESS) {
            continue;
        }

        char deviceName[NVML_DEVICE_NAME_BUFFER_SIZE]{};
        if (nvmlDeviceGetName(device, deviceName, sizeof(deviceName)) != NVML_SUCCESS) {
            wsprintfA(deviceName, "GPU %u", index);
        }

        GpuInfo gpu;
        gpu.device = device;
        gpu.name = ToWide(deviceName);
        gpu.driverMode = GetDriverMode(device, gpu.driverModeValid);
        state.gpus.push_back(gpu);
    }

    if (state.gpus.empty()) {
        state.status = L"No NVIDIA GPUs found";
        return;
    }

    UpdateGpuStatus(state);
    Refresh(state);
}

void DrawText(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color,
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    const HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, format);
    SelectObject(dc, previous);
}

void FillRectColor(HDC dc, const RECT& bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

std::wstring FormatWindowDuration(unsigned int seconds) {
    if (seconds < 3600) {
        const unsigned int minutes = seconds / 60;
        return std::to_wstring(minutes) + (minutes == 1 ? L" minute ago" : L" minutes ago");
    }

    const unsigned int hours = seconds / 3600;
    return std::to_wstring(hours) + (hours == 1 ? L" hour ago" : L" hours ago");
}

int HistoryPointX(const Sample& sample, int left, int plotWidth,
    ULONGLONG windowStart, ULONGLONG windowMs) {
    const ULONGLONG elapsedMs = sample.timestampMs > windowStart
        ? std::min(sample.timestampMs - windowStart, windowMs)
        : 0;
    return left + static_cast<int>(plotWidth * elapsedMs / windowMs);
}

void DrawGraph(HDC dc, const GpuInfo& gpu, RECT bounds, const Fonts& fonts,
    ULONGLONG now, ULONGLONG windowMs, unsigned int windowSeconds) {
    FillRectColor(dc, bounds, kPlotBackground);

    const int left = bounds.left + 42;
    const int right = bounds.right - 12;
    const int top = bounds.top + 14;
    const int bottom = bounds.bottom - 20;
    if (right <= left || bottom <= top) {
        return;
    }

    HPEN gridPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ previousPen = SelectObject(dc, gridPen);
    for (int division = 0; division <= 4; ++division) {
        const int y = top + (bottom - top) * division / 4;
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, right, y);
    }
    SelectObject(dc, previousPen);
    DeleteObject(gridPen);

    DrawText(dc, L"100", {bounds.left + 4, top - 8, bounds.left + 36, top + 8},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawText(dc, L"50", {bounds.left + 4, (top + bottom) / 2 - 8, bounds.left + 36,
        (top + bottom) / 2 + 8}, fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawText(dc, L"0", {bounds.left + 4, bottom - 8, bounds.left + 36, bottom + 8},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    const ULONGLONG windowStart = now > windowMs ? now - windowMs : 0;
    std::vector<PixelRange> ranges(static_cast<size_t>(plotWidth + 1));

    for (int offset = 0; offset < gpu.historyCount; ++offset) {
        const int index = (gpu.historyNext - 1 - offset + kHistoryCapacity)
            % kHistoryCapacity;
        const Sample& sample = gpu.history[index];
        if (sample.timestampMs < windowStart) {
            break;
        }
        if (sample.timestampMs > now) {
            continue;
        }

        const int bucket = HistoryPointX(sample, left, plotWidth, windowStart, windowMs) - left;
        PixelRange& range = ranges[bucket];
        if (!sample.valid) {
            continue;
        }
        const unsigned int load = std::min(sample.load, 100U);
        range.minimum = std::min(range.minimum, static_cast<unsigned long long>(load));
        range.maximum = std::max(range.maximum, static_cast<unsigned long long>(load));
        range.hasValue = true;
    }

    if (gpu.historyCount > 0) {
        const Sample& latestSample = gpu.history[
            (gpu.historyNext - 1 + kHistoryCapacity) % kHistoryCapacity];
        if (latestSample.valid && latestSample.timestampMs >= windowStart
            && latestSample.timestampMs <= now
            && HistoryPointX(latestSample, left, plotWidth, windowStart, windowMs) < right) {
            PixelRange& currentRange = ranges[plotWidth];
            const unsigned int load = std::min(latestSample.load, 100U);
            currentRange.minimum = std::min(
                currentRange.minimum, static_cast<unsigned long long>(load));
            currentRange.maximum = std::max(
                currentRange.maximum, static_cast<unsigned long long>(load));
            currentRange.hasValue = true;
        }
    }

    HPEN loadPen = CreatePen(PS_SOLID, 2, kAccent);
    previousPen = SelectObject(dc, loadPen);
    POINT previousCenter{};
    bool hasPreviousCenter = false;
    for (int bucket = 0; bucket <= plotWidth; ++bucket) {
        const PixelRange& range = ranges[bucket];
        if (!range.hasValue) {
            continue;
        }
        const int highY = bottom - plotHeight * static_cast<int>(range.maximum) / 100;
        const int lowY = bottom - plotHeight * static_cast<int>(range.minimum) / 100;
        const POINT center{left + bucket, (highY + lowY) / 2};
        if (hasPreviousCenter) {
            MoveToEx(dc, previousCenter.x, previousCenter.y, nullptr);
            LineTo(dc, center.x, center.y);
        }
        MoveToEx(dc, left + bucket, highY, nullptr);
        LineTo(dc, left + bucket, lowY == highY ? lowY + 1 : lowY);
        previousCenter = center;
        hasPreviousCenter = true;
    }
    SelectObject(dc, previousPen);
    DeleteObject(loadPen);

    DrawText(dc, FormatWindowDuration(windowSeconds),
        {left, bounds.bottom - 19, left + 150, bounds.bottom}, fonts.body, kMutedText);
    DrawText(dc, L"now", {right - 42, bounds.bottom - 19, right, bounds.bottom},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

std::wstring FormatMemory(unsigned long long bytes) {
    const unsigned long long tenthsOfGigabytes =
        (bytes * 10 + (1ULL << 29)) / (1ULL << 30);
    return std::to_wstring(tenthsOfGigabytes / 10) + L"."
        + std::to_wstring(tenthsOfGigabytes % 10) + L" GB";
}

void DrawMemoryGraph(HDC dc, const GpuInfo& gpu, RECT bounds, const Fonts& fonts,
    ULONGLONG now, ULONGLONG windowMs, unsigned int windowSeconds) {
    FillRectColor(dc, bounds, kPlotBackground);

    const int left = bounds.left + 62;
    const int right = bounds.right - 12;
    const int top = bounds.top + 14;
    const int bottom = bounds.bottom - 20;
    if (right <= left || bottom <= top) {
        return;
    }

    const ULONGLONG windowStart = now > windowMs ? now - windowMs : 0;
    const unsigned long long maxMemory = gpu.memoryTotal;
    if (maxMemory == 0) {
        DrawText(dc, L"Memory data unavailable", {left, top, right, bottom},
            fonts.body, kMutedText);
        return;
    }

    HPEN gridPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ previousPen = SelectObject(dc, gridPen);
    for (int division = 0; division <= 2; ++division) {
        const int y = top + (bottom - top) * division / 2;
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, right, y);
    }
    SelectObject(dc, previousPen);
    DeleteObject(gridPen);

    DrawText(dc, FormatMemory(maxMemory),
        {bounds.left + 2, top - 8, left - 4, top + 8}, fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawText(dc, FormatMemory(maxMemory / 2),
        {bounds.left + 2, (top + bottom) / 2 - 8, left - 4, (top + bottom) / 2 + 8},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawText(dc, L"0 GB", {bounds.left + 2, bottom - 8, left - 4, bottom + 8},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    std::vector<PixelRange> ranges(static_cast<size_t>(plotWidth + 1));

    for (int offset = 0; offset < gpu.historyCount; ++offset) {
        const int index = (gpu.historyNext - 1 - offset + kHistoryCapacity)
            % kHistoryCapacity;
        const Sample& sample = gpu.history[index];
        if (sample.timestampMs < windowStart) {
            break;
        }
        if (sample.timestampMs > now) {
            continue;
        }

        const int bucket = HistoryPointX(sample, left, plotWidth, windowStart, windowMs) - left;
        PixelRange& range = ranges[bucket];
        if (!sample.memoryValid) {
            continue;
        }
        const unsigned long long memoryUsed = std::min(sample.memoryUsed, maxMemory);
        range.minimum = std::min(range.minimum, memoryUsed);
        range.maximum = std::max(range.maximum, memoryUsed);
        range.hasValue = true;
    }

    if (gpu.historyCount > 0) {
        const Sample& latestSample = gpu.history[
            (gpu.historyNext - 1 + kHistoryCapacity) % kHistoryCapacity];
        if (latestSample.memoryValid && latestSample.timestampMs >= windowStart
            && latestSample.timestampMs <= now
            && HistoryPointX(latestSample, left, plotWidth, windowStart, windowMs) < right) {
            PixelRange& currentRange = ranges[plotWidth];
            const unsigned long long memoryUsed =
                std::min(latestSample.memoryUsed, maxMemory);
            currentRange.minimum = std::min(currentRange.minimum, memoryUsed);
            currentRange.maximum = std::max(currentRange.maximum, memoryUsed);
            currentRange.hasValue = true;
        }
    }

    HPEN memoryPen = CreatePen(PS_SOLID, 2, kMemory);
    previousPen = SelectObject(dc, memoryPen);
    POINT previousCenter{};
    bool hasPreviousCenter = false;
    for (int bucket = 0; bucket <= plotWidth; ++bucket) {
        const PixelRange& range = ranges[bucket];
        if (!range.hasValue) {
            continue;
        }
        const int highY = bottom - static_cast<int>(plotHeight * range.maximum / maxMemory);
        const int lowY = bottom - static_cast<int>(plotHeight * range.minimum / maxMemory);
        const POINT center{left + bucket, (highY + lowY) / 2};
        if (hasPreviousCenter) {
            MoveToEx(dc, previousCenter.x, previousCenter.y, nullptr);
            LineTo(dc, center.x, center.y);
        }
        MoveToEx(dc, left + bucket, highY, nullptr);
        LineTo(dc, left + bucket, lowY == highY ? lowY + 1 : lowY);
        previousCenter = center;
        hasPreviousCenter = true;
    }
    SelectObject(dc, previousPen);
    DeleteObject(memoryPen);

    DrawText(dc, FormatWindowDuration(windowSeconds),
        {left, bounds.bottom - 19, left + 150, bounds.bottom}, fonts.body, kMutedText);
    DrawText(dc, L"now", {right - 42, bounds.bottom - 19, right, bounds.bottom},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawGpuCard(HDC dc, const GpuInfo& gpu, RECT card, const Fonts& fonts,
    ULONGLONG now, ULONGLONG windowMs, unsigned int windowSeconds) {
    FillRectColor(dc, card, kCardBackground);

    std::wstring name = gpu.name;
    if (gpu.driverModeValid) {
        name += L" (" + gpu.driverMode + L")";
    }
    RECT nameBounds{card.left + 20, card.top + 12, card.right - 18, card.top + 40};
    DrawText(dc, name, nameBounds, fonts.heading, kPrimaryText);

    RECT loadLabel{card.left + 20, card.top + 49, card.left + 166, card.top + 70};
    DrawText(dc, L"GPU LOAD", loadLabel, fonts.body, kMutedText);

    const std::wstring loadText = gpu.loadValid ? std::to_wstring(gpu.load) + L"%" : L"--";
    RECT loadValue{card.left + 18, card.top + 65, card.left + 188, card.top + 116};
    DrawText(dc, loadText, loadValue, fonts.metric, gpu.loadValid ? kAccent : kWarning);

    RECT temperatureLabel{card.left + 20, card.top + 119, card.left + 180, card.top + 140};
    DrawText(dc, L"TEMPERATURE", temperatureLabel, fonts.body, kMutedText);

    const std::wstring temperatureText = gpu.temperatureValid
        ? std::to_wstring(gpu.temperature) + L" C"
        : L"--";
    RECT temperatureValue{card.left + 20, card.top + 138, card.left + 180, card.top + 166};
    DrawText(dc, temperatureText, temperatureValue, fonts.heading,
        gpu.temperatureValid ? kTemperature : kWarning);

    const Sample& latestSample = gpu.history[
        (gpu.historyNext - 1 + kHistoryCapacity) % kHistoryCapacity];
    RECT memoryLabel{card.left + 20, card.top + 178, card.left + 180, card.top + 199};
    DrawText(dc, L"MEMORY USAGE", memoryLabel, fonts.body, kMutedText);

    const std::wstring memoryText = latestSample.memoryValid
        ? FormatMemory(latestSample.memoryUsed)
        : L"--";
    RECT memoryValue{card.left + 20, card.top + 198, card.left + 180, card.top + 232};
    DrawText(dc, memoryText, memoryValue, fonts.heading,
        latestSample.memoryValid ? kMemory : kWarning);

    RECT graphBounds{card.left + 198, card.top + 48, card.right - 16, card.bottom - 12};
    const int graphMiddle = (graphBounds.top + graphBounds.bottom) / 2;
    DrawGraph(dc, gpu, {graphBounds.left, graphBounds.top, graphBounds.right, graphMiddle},
        fonts, now, windowMs, windowSeconds);
    DrawMemoryGraph(dc, gpu,
        {graphBounds.left, graphMiddle, graphBounds.right, graphBounds.bottom}, fonts,
        now, windowMs, windowSeconds);
}

void DrawCombinedVramRow(HDC dc, const AppState& state, RECT bounds, const Fonts& fonts) {
    FillRectColor(dc, bounds, kCardBackground);

    RECT labelBounds{bounds.left + 20, bounds.top + 10, bounds.left + 230,
        bounds.top + 32};
    DrawText(dc, L"COMBINED VRAM", labelBounds, fonts.body, kMutedText);

    unsigned long long used = 0;
    unsigned long long total = 0;
    bool valid = true;
    for (const GpuInfo& gpu : state.gpus) {
        if (gpu.historyCount == 0) {
            valid = false;
            break;
        }
        const Sample& latestSample = gpu.history[
            (gpu.historyNext - 1 + kHistoryCapacity) % kHistoryCapacity];
        if (!latestSample.memoryValid) {
            valid = false;
            break;
        }
        used += latestSample.memoryUsed;
        total += latestSample.memoryTotal;
    }

    const std::wstring memoryText = valid
        ? FormatMemory(used) + L" / " + FormatMemory(total)
        : L"-- / --";
    RECT valueBounds{bounds.left + 20, bounds.top + 35, bounds.left + 330,
        bounds.bottom - 10};
    DrawText(dc, memoryText, valueBounds, fonts.heading, valid ? kMemory : kWarning);

    RECT barBounds{bounds.left + 350, bounds.top + 40, bounds.right - 74, bounds.top + 54};
    FillRectColor(dc, barBounds, kPlotBackground);
    if (valid && total > 0) {
        RECT usedBounds = barBounds;
        usedBounds.right = usedBounds.left + static_cast<int>(
            (barBounds.right - barBounds.left) * used / total);
        FillRectColor(dc, usedBounds, kMemory);
        const unsigned long long percentage = used * 100 / total;
        RECT percentageBounds{bounds.right - 66, bounds.top + 32, bounds.right - 18,
            bounds.top + 62};
        DrawText(dc, std::to_wstring(percentage) + L"%", percentageBounds,
            fonts.heading, kMemory, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

int ContentHeight(const AppState& state) {
    if (state.gpus.empty()) {
        return kHeaderHeight + 100;
    }
    int height = kHeaderHeight + 16
        + static_cast<int>(state.gpus.size()) * (kCardHeight + kCardGap);
    if (state.gpus.size() > 1) {
        height += kCardGap + kCombinedVramRowHeight;
    }
    return height;
}

void PositionHeaderControls(HWND window, const AppState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int updateX = std::max(24,
        static_cast<int>(client.right) - 24 - kUpdateSpeedComboWidth);
    const int windowX = updateX - kHeaderGroupOffset;
    if (state.updateSpeedCombo) {
        MoveWindow(state.updateSpeedCombo, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.updateSpeedButton) {
        MoveWindow(state.updateSpeedButton, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
    if (state.windowSizeCombo) {
        MoveWindow(state.windowSizeCombo, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.windowSizeButton) {
        MoveWindow(state.windowSizeButton, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
}

void OverlapDropdownBorder(HWND combo, HWND button) {
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    RECT listBounds{};
    RECT buttonBounds{};
    if (!GetComboBoxInfo(combo, &comboInfo) || !comboInfo.hwndList
        || !GetWindowRect(comboInfo.hwndList, &listBounds)
        || !GetWindowRect(button, &buttonBounds)) {
        return;
    }

    int top = listBounds.top;
    int height = listBounds.bottom - listBounds.top;
    if (listBounds.top >= buttonBounds.bottom) {
        --top;
        ++height;
    } else if (listBounds.bottom <= buttonBounds.top) {
        ++height;
    } else {
        return;
    }

    SetWindowPos(comboInfo.hwndList, nullptr, listBounds.left, top,
        listBounds.right - listBounds.left, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void EnableDarkTitleBar(HWND window) {
    HMODULE dwmLibrary = LoadLibraryW(L"dwmapi.dll");
    if (dwmLibrary == nullptr) {
        return;
    }

    using SetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const auto setWindowAttribute = reinterpret_cast<SetWindowAttributeFunction>(
        GetProcAddress(dwmLibrary, "DwmSetWindowAttribute"));
    if (setWindowAttribute != nullptr) {
        const BOOL enabled = TRUE;
        constexpr DWORD kImmersiveDarkMode = 20;
        constexpr DWORD kImmersiveDarkModeLegacy = 19;
        if (FAILED(setWindowAttribute(window, kImmersiveDarkMode, &enabled, sizeof(enabled)))) {
            setWindowAttribute(window, kImmersiveDarkModeLegacy, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(dwmLibrary);
}

void FitWindowToContent(HWND window, const AppState& state) {
    RECT client{};
    RECT windowBounds{};
    GetClientRect(window, &client);
    GetWindowRect(window, &windowBounds);

    RECT desiredBounds{0, 0, static_cast<LONG>(client.right),
        std::max(static_cast<int>(client.bottom), ContentHeight(state))};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&desiredBounds, style, FALSE, extendedStyle)) {
        return;
    }

    int width = desiredBounds.right - desiredBounds.left;
    int height = desiredBounds.bottom - desiredBounds.top;
    int x = windowBounds.left;
    int y = windowBounds.top;

    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        const RECT& workArea = monitorInfo.rcWork;
        width = std::min(width, static_cast<int>(workArea.right - workArea.left));
        height = std::min(height, static_cast<int>(workArea.bottom - workArea.top));
        x = std::clamp(x, static_cast<int>(workArea.left),
            static_cast<int>(workArea.right) - width);
        y = std::clamp(y, static_cast<int>(workArea.top),
            static_cast<int>(workArea.bottom) - height);
    }

    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void UpdateScrollBar(HWND window, AppState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int maxPosition = std::max(0, ContentHeight(state) - clientHeight);
    state.scrollPosition = std::clamp(state.scrollPosition, 0, maxPosition);

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, ContentHeight(state) - 1);
    info.nPage = static_cast<UINT>(client.bottom - client.top);
    info.nPos = state.scrollPosition;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void SetScrollPosition(HWND window, AppState& state, int position) {
    state.scrollPosition = position;
    UpdateScrollBar(window, state);
    InvalidateRect(window, nullptr, FALSE);
}

void PaintWindow(HWND window, AppState& state) {
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);
    HDC bufferDc = CreateCompatibleDC(targetDc);
    HBITMAP bufferBitmap = width > 0 && height > 0
        ? CreateCompatibleBitmap(targetDc, width, height)
        : nullptr;
    HGDIOBJ previousBitmap = bufferDc && bufferBitmap
        ? SelectObject(bufferDc, bufferBitmap)
        : nullptr;
    const bool buffered = bufferDc != nullptr && bufferBitmap != nullptr
        && previousBitmap != nullptr && previousBitmap != HGDI_ERROR;
    HDC dc = buffered ? bufferDc : targetDc;

    FillRectColor(dc, client, kBackground);

    const int updateComboX = std::max(24,
        static_cast<int>(client.right) - 24 - kUpdateSpeedComboWidth);
    const int windowComboX = updateComboX - kHeaderGroupOffset;
    RECT windowSizeLabel{windowComboX - 76, 15, windowComboX - 8, 42};
    DrawText(dc, L"WINDOW", windowSizeLabel, state.fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT updateLabel{updateComboX - 76, 15, updateComboX - 8, 42};
    DrawText(dc, L"UPDATE", updateLabel, state.fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT statusBounds{26, 15, windowComboX - 88, 42};
    const COLORREF statusColor = state.nvmlInitialized ? kMutedText : kWarning;
    DrawText(dc, state.status, statusBounds, state.fonts.body, statusColor);

    if (state.gpus.empty()) {
        RECT messageBounds{24, kHeaderHeight + 20, client.right - 24, kHeaderHeight + 70};
        DrawText(dc, state.status.empty() ? L"Waiting for NVML" : state.status,
            messageBounds, state.fonts.heading, kPrimaryText);
    } else {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG windowMs =
            static_cast<ULONGLONG>(state.graphWindowSeconds) * 1000;
        for (size_t index = 0; index < state.gpus.size(); ++index) {
            const int top = kHeaderHeight + 12 + static_cast<int>(index) * (kCardHeight + kCardGap)
                - state.scrollPosition;
            RECT card{18, top, client.right - 18, top + kCardHeight};
            DrawGpuCard(dc, state.gpus[index], card, state.fonts,
                now, windowMs, state.graphWindowSeconds);
        }
        if (state.gpus.size() > 1) {
            const int top = kHeaderHeight + 12
                + static_cast<int>(state.gpus.size()) * (kCardHeight + kCardGap)
                - state.scrollPosition;
            RECT summary{18, top, client.right - 18, top + kCombinedVramRowHeight};
            DrawCombinedVramRow(dc, state, summary, state.fonts);
        }
    }

    if (buffered) {
        BitBlt(targetDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, previousBitmap);
    }
    if (bufferBitmap) DeleteObject(bufferBitmap);
    if (bufferDc) DeleteDC(bufferDc);
    EndPaint(window, &paint);
}

void DestroyFonts(Fonts& fonts) {
    if (fonts.title) DeleteObject(fonts.title);
    if (fonts.heading) DeleteObject(fonts.heading);
    if (fonts.body) DeleteObject(fonts.body);
    if (fonts.metric) DeleteObject(fonts.metric);
}

void CreateFonts(Fonts& fonts) {
    fonts.title = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    fonts.heading = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    fonts.body = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    fonts.metric = CreateFontW(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        LoadSettings(*state);
        SetWindowTextW(window, L"GPU Monitor");
        EnableDarkTitleBar(window);
        CreateFonts(state->fonts);
        state->dropdownBackground = CreateSolidBrush(kDropdownBackground);
        state->dropdownBorder = CreateSolidBrush(kDropdownBorder);
        state->updateSpeedCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VSCROLL
                | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
            0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateSpeedControlId)),
            GetModuleHandleW(nullptr), nullptr);
        if (state->updateSpeedCombo) {
            SendMessageW(state->updateSpeedCombo, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
            for (const SamplingOption& option : kSamplingOptions) {
                SendMessageW(state->updateSpeedCombo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(option.label));
            }
            SendMessageW(state->updateSpeedCombo, CB_SETCURSEL,
                FindSamplingOption(*state), 0);
            state->updateSpeedButton = CreateWindowExW(0, L"BUTTON", nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedComboHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateSpeedButtonId)),
                GetModuleHandleW(nullptr), nullptr);
        }
        state->windowSizeCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VSCROLL
                | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
            0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowSizeControlId)),
            GetModuleHandleW(nullptr), nullptr);
        if (state->windowSizeCombo) {
            SendMessageW(state->windowSizeCombo, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
            for (const WindowSizeOption& option : kWindowSizeOptions) {
                SendMessageW(state->windowSizeCombo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(option.label));
            }
            SendMessageW(state->windowSizeCombo, CB_SETCURSEL,
                FindWindowSizeOption(*state), 0);
            state->windowSizeButton = CreateWindowExW(0, L"BUTTON", nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedComboHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowSizeButtonId)),
                GetModuleHandleW(nullptr), nullptr);
        }
        PositionHeaderControls(window, *state);
        InitializeNvml(*state);
        SetTimer(window, kTimerId, state->sampleIntervalMs, nullptr);
        SetTimer(window, kGraphRefreshTimerId, 1000, nullptr);
        UpdateScrollBar(window, *state);
        return 0;
    case WM_COMMAND:
        if (state && LOWORD(wParam) == kUpdateSpeedButtonId
            && HIWORD(wParam) == BN_CLICKED) {
            EnableWindow(state->updateSpeedButton, FALSE);
            ShowWindow(state->updateSpeedCombo, SW_SHOW);
            SetWindowPos(state->updateSpeedButton, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetFocus(state->updateSpeedCombo);
            SendMessageW(state->updateSpeedCombo, CB_SHOWDROPDOWN, TRUE, 0);
            OverlapDropdownBorder(state->updateSpeedCombo, state->updateSpeedButton);
            return 0;
        }
        if (state && LOWORD(wParam) == kWindowSizeButtonId
            && HIWORD(wParam) == BN_CLICKED) {
            EnableWindow(state->windowSizeButton, FALSE);
            ShowWindow(state->windowSizeCombo, SW_SHOW);
            SetWindowPos(state->windowSizeButton, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetFocus(state->windowSizeCombo);
            SendMessageW(state->windowSizeCombo, CB_SHOWDROPDOWN, TRUE, 0);
            OverlapDropdownBorder(state->windowSizeCombo, state->windowSizeButton);
            return 0;
        }
        if (state && LOWORD(wParam) == kUpdateSpeedControlId
            && HIWORD(wParam) == CBN_CLOSEUP) {
            ShowWindow(state->updateSpeedCombo, SW_HIDE);
            EnableWindow(state->updateSpeedButton, TRUE);
            InvalidateRect(state->updateSpeedButton, nullptr, FALSE);
            return 0;
        }
        if (state && LOWORD(wParam) == kUpdateSpeedControlId
            && HIWORD(wParam) == CBN_SELCHANGE) {
            const LRESULT selection = SendMessageW(state->updateSpeedCombo, CB_GETCURSEL, 0, 0);
            if (selection >= 0 && selection < static_cast<LRESULT>(kSamplingOptions.size())) {
                state->sampleIntervalMs = kSamplingOptions[selection].intervalMs;
                SaveSettings(*state);
                SetTimer(window, kTimerId, state->sampleIntervalMs, nullptr);
                if (!state->gpus.empty()) {
                    UpdateGpuStatus(*state);
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        if (state && LOWORD(wParam) == kWindowSizeControlId
            && HIWORD(wParam) == CBN_CLOSEUP) {
            ShowWindow(state->windowSizeCombo, SW_HIDE);
            EnableWindow(state->windowSizeButton, TRUE);
            InvalidateRect(state->windowSizeButton, nullptr, FALSE);
            return 0;
        }
        if (state && LOWORD(wParam) == kWindowSizeControlId
            && HIWORD(wParam) == CBN_SELCHANGE) {
            const LRESULT selection = SendMessageW(state->windowSizeCombo,
                CB_GETCURSEL, 0, 0);
            if (selection >= 0
                && selection < static_cast<LRESULT>(kWindowSizeOptions.size())) {
                state->graphWindowSeconds = kWindowSizeOptions[selection].seconds;
                SaveSettings(*state);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_CTLCOLORLISTBOX:
        if (state && state->dropdownBackground) {
            const std::array<HWND, 2> combos{state->updateSpeedCombo, state->windowSizeCombo};
            for (HWND combo : combos) {
                COMBOBOXINFO info{sizeof(info)};
                if (combo && GetComboBoxInfo(combo, &info)
                    && reinterpret_cast<HWND>(lParam) == info.hwndList) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), kDropdownText);
                    SetBkColor(reinterpret_cast<HDC>(wParam), kDropdownBackground);
                    return reinterpret_cast<LRESULT>(state->dropdownBackground);
                }
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_DRAWITEM:
        if (state && state->dropdownBackground) {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            HWND combo = nullptr;
            if (item->CtlType == ODT_BUTTON && item->CtlID == kUpdateSpeedButtonId) {
                combo = state->updateSpeedCombo;
            } else if (item->CtlType == ODT_BUTTON
                && item->CtlID == kWindowSizeButtonId) {
                combo = state->windowSizeCombo;
            }
            if (combo) {
                FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                if (state->dropdownBorder) {
                    FrameRect(item->hDC, &item->rcItem, state->dropdownBorder);
                }

                wchar_t label[32]{};
                const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                if (selection >= 0) {
                    SendMessageW(combo, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(label));
                }
                const HGDIOBJ previousFont = SelectObject(item->hDC, state->fonts.body);
                SetTextColor(item->hDC, kDropdownText);
                SetBkMode(item->hDC, TRANSPARENT);
                RECT textBounds = item->rcItem;
                textBounds.left += 8;
                textBounds.right -= 22;
                DrawTextW(item->hDC, label, -1, &textBounds,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(item->hDC, previousFont);

                POINT arrow[] = {
                    {item->rcItem.right - 16, item->rcItem.top + 11},
                    {item->rcItem.right - 8, item->rcItem.top + 11},
                    {item->rcItem.right - 12, item->rcItem.top + 16},
                };
                HPEN arrowPen = CreatePen(PS_SOLID, 1, kDropdownText);
                HBRUSH arrowBrush = CreateSolidBrush(kDropdownText);
                const HGDIOBJ previousPen = SelectObject(item->hDC, arrowPen);
                const HGDIOBJ previousBrush = SelectObject(item->hDC, arrowBrush);
                Polygon(item->hDC, arrow, static_cast<int>(std::size(arrow)));
                SelectObject(item->hDC, previousPen);
                SelectObject(item->hDC, previousBrush);
                DeleteObject(arrowPen);
                DeleteObject(arrowBrush);
                return TRUE;
            }
            combo = nullptr;
            if (item->CtlType == ODT_COMBOBOX && item->CtlID == kUpdateSpeedControlId) {
                combo = state->updateSpeedCombo;
            } else if (item->CtlType == ODT_COMBOBOX
                && item->CtlID == kWindowSizeControlId) {
                combo = state->windowSizeCombo;
            }
            if (combo) {
                FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                if (item->itemID != static_cast<UINT>(-1)) {
                    wchar_t label[32]{};
                    SendMessageW(combo, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(label));
                    const HGDIOBJ previousFont = SelectObject(item->hDC, state->fonts.body);
                    SetTextColor(item->hDC, kDropdownText);
                    SetBkMode(item->hDC, TRANSPARENT);
                    RECT textBounds = item->rcItem;
                    textBounds.left += 8;
                    textBounds.right -= 18;
                    DrawTextW(item->hDC, label, -1, &textBounds,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(item->hDC, previousFont);
                }
                return TRUE;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_TIMER:
        if (state && wParam == kGraphRefreshTimerId) {
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (state && wParam == kTimerId && state->nvmlInitialized) {
            Refresh(*state);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_SIZE:
        if (state) {
            PositionHeaderControls(window, *state);
            UpdateScrollBar(window, *state);
        }
        return 0;
    case WM_VSCROLL:
        if (state) {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &info);
            int position = state->scrollPosition;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: position -= 48; break;
            case SB_LINEDOWN: position += 48; break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: position = info.nTrackPos; break;
            case SB_THUMBPOSITION: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = info.nMax; break;
            default: return 0;
            }
            SetScrollPosition(window, *state, position);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            SetScrollPosition(window, *state,
                state->scrollPosition - delta / WHEEL_DELTA * 48);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 680;
        limits->ptMinTrackSize.y = 360;
        return 0;
    }
    case WM_PAINT:
        if (state) PaintWindow(window, *state);
        else return DefWindowProcW(window, message, wParam, lParam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (state) {
            KillTimer(window, kTimerId);
            KillTimer(window, kGraphRefreshTimerId);
            if (state->nvmlInitialized) nvmlShutdown();
            DestroyFonts(state->fonts);
            if (state->dropdownBackground) DeleteObject(state->dropdownBackground);
            if (state->dropdownBorder) DeleteObject(state->dropdownBorder);
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    AppState state;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_COMPOSITED, kWindowClass, L"GPU Monitor",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        nullptr, nullptr, instance, &state);
    if (!window) {
        return 1;
    }

    FitWindowToContent(window, state);
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
