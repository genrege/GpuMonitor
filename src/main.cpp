#include <windows.h>
#include <windowsx.h>
#include <nvml.h>
#include "resource.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <functional>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace
{

constexpr wchar_t kWindowClass[] = L"NvidiaInfoGpuMonitorWindow";
constexpr wchar_t kSettingsRegistryPath[] = L"Software\\NvidiaInfoGpuMonitor";
constexpr int kTimerId = 1;
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
constexpr int kPanelHeaderHeight = 40;
constexpr int kScrollStripHeight = 18;
constexpr int kSystemPanelExpandedHeight = kCardHeight;

struct SamplingOption
{
    const wchar_t* label;
    int intervalMs;
};

struct WindowSizeOption
{
    const wchar_t* label;
    unsigned int durationMs;
};

constexpr std::array<SamplingOption, 15> kSamplingOptions{{
    {L"100 ms", 100},
    {L"250 ms", 250},
    {L"500 ms", 500},
    {L"1 s", 1000},
    {L"2 s", 2000},
    {L"5 s", 5000},
    {L"10 s", 10000},
    {L"30 s", 30000},
    {L"1 minute", 60000},
    {L"2 minutes", 120000},
    {L"5 minutes", 300000},
    {L"10 minutes", 600000},
    {L"15 minutes", 900000},
    {L"30 minutes", 1800000},
    {L"1 hour", 3600000},
}};

constexpr std::array<WindowSizeOption, 12> kWindowSizeOptions{{
    {L"1 minute", 60000},
    {L"2 minutes", 120000},
    {L"5 minutes", 300000},
    {L"10 minutes", 600000},
    {L"15 minutes", 900000},
    {L"30 minutes", 1800000},
    {L"1 hour", 3600000},
    {L"2 hours", 7200000},
    {L"4 hours", 14400000},
    {L"8 hours", 28800000},
    {L"12 hours", 43200000},
    {L"24 hours", 86400000},
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
const COLORREF kTimeText = RGB(255, 235, 0);

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
};

struct PixelRange
{
    unsigned long long minimum = std::numeric_limits<unsigned long long>::max();
    unsigned long long maximum = 0;
    bool hasValue = false;
};

struct GpuInfo
{
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

struct Fonts
{
    HFONT title = nullptr;
    HFONT heading = nullptr;
    HFONT body = nullptr;
    HFONT metric = nullptr;
};

struct AppState
{
    std::vector<GpuInfo> gpus;
    SystemInfo system;
    unsigned int gpuCollapsedMask = 0;
    bool combinedCollapsed = false;
    bool systemCollapsed = false;
    std::wstring status;
    HWND updateSpeedCombo = nullptr;
    HWND updateSpeedButton = nullptr;
    HWND windowSizeCombo = nullptr;
    HWND windowSizeButton = nullptr;
    HBRUSH dropdownBackground = nullptr;
    HBRUSH dropdownBorder = nullptr;
    int sampleIntervalMs = kSampleIntervalMs;
    unsigned int graphWindowMs = 60000;
    bool nvmlInitialized = false;
    int scrollPosition = 0;
    bool followLive = true;
    bool showClockTime = false;
    bool paused = false;
    ULONGLONG pausedTick = 0;
    std::vector<GpuInfo> pausedGpus;
    SystemInfo pausedSystem;
    POINT mouse{};
    bool mouseInside = false;
    std::vector<RECT> axisLabelRects;
    ULONGLONG viewEndMs = 0;
    bool draggingScroll = false;
    int dragGrabX = 0;
    Fonts fonts;
};

void LoadSettings(AppState& state)
{
    DWORD sampleIntervalMs = 0;
    DWORD valueSize = sizeof(sampleIntervalMs);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, L"SampleIntervalMs",
            RRF_RT_REG_DWORD, nullptr, &sampleIntervalMs, &valueSize) == ERROR_SUCCESS)
    {
        for (const SamplingOption& option : kSamplingOptions)
        {
            if (sampleIntervalMs == static_cast<DWORD>(option.intervalMs))
            {
                state.sampleIntervalMs = option.intervalMs;
                break;
            }
        }
    }

    DWORD graphWindowMs = 0;
    valueSize = sizeof(graphWindowMs);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, L"GraphWindowMs",
            RRF_RT_REG_DWORD, nullptr, &graphWindowMs, &valueSize) == ERROR_SUCCESS)
    {
        for (const WindowSizeOption& option : kWindowSizeOptions)
        {
            if (graphWindowMs == option.durationMs)
            {
                state.graphWindowMs = option.durationMs;
                break;
            }
        }
    }

    const auto readFlag = [](const wchar_t* name, DWORD& value)
    {
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, name,
                   RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
    };
    DWORD flag = 0;
    if (readFlag(L"GpuCollapsedMask", flag))
        state.gpuCollapsedMask = flag;
    if (readFlag(L"CombinedCollapsed", flag))
        state.combinedCollapsed = flag != 0;
    if (readFlag(L"SystemCollapsed", flag))
        state.systemCollapsed = flag != 0;
    if (readFlag(L"ShowClockTime", flag))
        state.showClockTime = flag != 0;
}

void SaveSettings(const AppState& state)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsRegistryPath, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    const DWORD sampleIntervalMs = static_cast<DWORD>(state.sampleIntervalMs);
    RegSetValueExW(key, L"SampleIntervalMs", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&sampleIntervalMs), sizeof(sampleIntervalMs));
    const DWORD graphWindowMs = state.graphWindowMs;
    RegSetValueExW(key, L"GraphWindowMs", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&graphWindowMs), sizeof(graphWindowMs));
    const auto writeFlag = [key](const wchar_t* name, DWORD value)
    {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
            sizeof(value));
    };
    writeFlag(L"GpuCollapsedMask", state.gpuCollapsedMask);
    writeFlag(L"CombinedCollapsed", state.combinedCollapsed ? 1 : 0);
    writeFlag(L"SystemCollapsed", state.systemCollapsed ? 1 : 0);
    writeFlag(L"ShowClockTime", state.showClockTime ? 1 : 0);
    RegCloseKey(key);
}

LRESULT FindSamplingOption(const AppState& state)
{
    for (size_t index = 0; index < kSamplingOptions.size(); ++index)
    {
        if (state.sampleIntervalMs == kSamplingOptions[index].intervalMs)
        {
            return static_cast<LRESULT>(index);
        }
    }
    return 0;
}

LRESULT FindWindowSizeOption(const AppState& state)
{
    for (size_t index = 0; index < kWindowSizeOptions.size(); ++index)
    {
        if (state.graphWindowMs == kWindowSizeOptions[index].durationMs)
        {
            return static_cast<LRESULT>(index);
        }
    }
    return 0;
}

std::wstring ToWide(const char* text)
{
    if (text == nullptr || *text == '\0')
    {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1)
    {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

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
    nvmlUtilization_t utilization{};
    gpu.loadValid = nvmlDeviceGetUtilizationRates(gpu.device, &utilization) == NVML_SUCCESS;
    if (gpu.loadValid)
    {
        gpu.load = utilization.gpu;
    }

    gpu.temperatureValid = nvmlDeviceGetTemperature(
                               gpu.device, NVML_TEMPERATURE_GPU, &gpu.temperature) == NVML_SUCCESS;

    nvmlMemory_t memory{};
    const bool memoryValid = nvmlDeviceGetMemoryInfo(gpu.device, &memory) == NVML_SUCCESS && memory.total > 0;
    if (memoryValid)
    {
        gpu.memoryTotal = memory.total;
    }
    gpu.history[gpu.historyNext] = {
        gpu.load, memory.used, memory.total, gpu.loadValid, memoryValid, timestampMs};
    gpu.historyNext = (gpu.historyNext + 1) % kHistoryCapacity;
    gpu.historyCount = std::min(gpu.historyCount + 1, kHistoryCapacity);
}

void Refresh(AppState& state)
{
    const ULONGLONG timestampMs = GetTickCount64();
    for (GpuInfo& gpu : state.gpus)
    {
        AddSample(gpu, timestampMs);
    }
    AddSystemSample(state.system, timestampMs);
}

std::wstring SamplingIntervalLabel(int intervalMs)
{
    for (const SamplingOption& option : kSamplingOptions)
    {
        if (option.intervalMs == intervalMs)
        {
            return option.label;
        }
    }
    return L"2 s";
}

void UpdateGpuStatus(AppState& state)
{
    state.status = std::to_wstring(state.gpus.size()) + L" NVIDIA GPU";
    if (state.gpus.size() != 1)
    {
        state.status += L"s";
    }
    state.status += L"  |  Update every " + SamplingIntervalLabel(state.sampleIntervalMs);
}

void InitializeNvml(AppState& state)
{
    nvmlReturn_t result = nvmlInit_v2();
    if (result != NVML_SUCCESS)
    {
        state.status = NvmlError("NVML initialization failed", result);
        return;
    }
    state.nvmlInitialized = true;

    unsigned int deviceCount = 0;
    result = nvmlDeviceGetCount_v2(&deviceCount);
    if (result != NVML_SUCCESS)
    {
        state.status = NvmlError("Could not enumerate GPUs", result);
        return;
    }

    for (unsigned int index = 0; index < deviceCount; ++index)
    {
        nvmlDevice_t device = nullptr;
        result = nvmlDeviceGetHandleByIndex_v2(index, &device);
        if (result != NVML_SUCCESS)
        {
            continue;
        }

        char deviceName[NVML_DEVICE_NAME_BUFFER_SIZE]{};
        if (nvmlDeviceGetName(device, deviceName, sizeof(deviceName)) != NVML_SUCCESS)
        {
            wsprintfA(deviceName, "GPU %u", index);
        }

        GpuInfo gpu;
        gpu.device = device;
        gpu.name = ToWide(deviceName);
        gpu.driverMode = GetDriverMode(device, gpu.driverModeValid);
        state.gpus.push_back(gpu);
    }

    if (state.gpus.empty())
    {
        state.status = L"No NVIDIA GPUs found";
        return;
    }

    UpdateGpuStatus(state);
    Refresh(state);
}

void DrawText(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color,
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
    const HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, format);
    SelectObject(dc, previous);
}

void FillRectColor(HDC dc, const RECT& bounds, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

std::wstring FormatAgo(ULONGLONG ms)
{
    const unsigned int total = static_cast<unsigned int>(ms / 1000);
    if (total < 1)
    {
        return L"now";
    }

    const unsigned int hours = total / 3600;
    const unsigned int minutes = total % 3600 / 60;
    const unsigned int seconds = total % 60;
    std::wstring text;
    const auto append = [&](unsigned int value, const wchar_t* unit)
    {
        if (value > 0)
        {
            text += (text.empty() ? L"" : L" ") + std::to_wstring(value) + L" " + unit;
        }
    };
    append(hours, L"h");
    append(minutes, L"min");
    append(seconds, L"s");
    return text + L" ago";
}

// The visible time range: [end - windowMs, end], with axis labels for its two edges.
struct TimeView
{
    ULONGLONG end = 0;
    ULONGLONG windowMs = 0;
    std::wstring leftLabel;
    std::wstring rightLabel;
    ULONGLONG now = 0;
    POINT mouse{};
    bool hasMouse = false;
    std::vector<RECT>* labelHits = nullptr;
};

struct ChartSpec
{
    std::wstring title;
    COLORREF color = kAccent;
    std::wstring topLabel;
    std::wstring midLabel;
    std::wstring bottomLabel;
    bool bars = false;
    bool showAxis = true;
    unsigned long long total = 0;
};

// Returns false past the oldest sample; fraction is 0..1, or negative when invalid.
using SampleAt = std::function<bool(int offset, ULONGLONG& timestampMs, double& fraction)>;

std::wstring FormatClock(ULONGLONG nowTick, ULONGLONG tick);

void DrawChart(HDC dc, RECT bounds, const ChartSpec& spec, const SampleAt& sampleAt,
    const Fonts& fonts, const TimeView& view)
{
    const ULONGLONG now = view.end;
    const ULONGLONG windowMs = view.windowMs;
    FillRectColor(dc, bounds, kPlotBackground);
    HBRUSH frameBrush = CreateSolidBrush(kGridColor);
    FrameRect(dc, &bounds, frameBrush);
    DeleteObject(frameBrush);

    const int left = bounds.left + 62;
    const int right = bounds.right - 12;
    const int top = bounds.top + 20;
    const int bottom = bounds.bottom - 20;
    if (right <= left || bottom <= top)
    {
        return;
    }

    DrawText(dc, spec.title, {bounds.left + 6, bounds.top + 1, right, bounds.top + 17},
        fonts.body, spec.color);

    HPEN gridPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ previousPen = SelectObject(dc, gridPen);
    for (int division = 0; division <= 4; ++division)
    {
        const int y = top + (bottom - top) * division / 4;
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, right, y);
    }
    SelectObject(dc, previousPen);
    DeleteObject(gridPen);

    const UINT labelFormat = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
    DrawText(dc, spec.topLabel, {bounds.left + 2, top - 8, left - 4, top + 8},
        fonts.body, kMutedText, labelFormat);
    DrawText(dc, spec.midLabel, {bounds.left + 2, (top + bottom) / 2 - 8, left - 4, (top + bottom) / 2 + 8}, fonts.body, kMutedText, labelFormat);
    DrawText(dc, spec.bottomLabel, {bounds.left + 2, bottom - 8, left - 4, bottom + 8},
        fonts.body, kMutedText, labelFormat);

    constexpr double kScale = 10000.0;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    const ULONGLONG windowStart = now > windowMs ? now - windowMs : 0;
    std::vector<PixelRange> ranges(static_cast<size_t>(plotWidth + 1));
    const auto addValue = [](PixelRange& range, unsigned long long value)
    {
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
        range.hasValue = true;
    };

    bool hasNewest = false;
    unsigned long long newestValue = 0;
    int newestBucket = 0;
    for (int offset = 0;; ++offset)
    {
        ULONGLONG timestampMs = 0;
        double fraction = 0;
        if (!sampleAt(offset, timestampMs, fraction) || timestampMs < windowStart)
        {
            break;
        }
        if (timestampMs > now || fraction < 0)
        {
            continue;
        }
        const ULONGLONG elapsedMs = std::min(timestampMs - windowStart, windowMs);
        const int bucket = static_cast<int>(plotWidth * elapsedMs / windowMs);
        const auto value = static_cast<unsigned long long>(
            std::clamp(fraction, 0.0, 1.0) * kScale);
        addValue(ranges[bucket], value);
        if (!hasNewest)
        {
            hasNewest = true;
            newestValue = value;
            newestBucket = bucket;
        }
    }
    // Extend the newest sample to the right edge so the series reaches "now".
    if (hasNewest && newestBucket < plotWidth)
    {
        addValue(ranges[plotWidth], newestValue);
    }

    const auto valueToY = [&](unsigned long long value)
    {
        return bottom - static_cast<int>(plotHeight * value / static_cast<unsigned long long>(kScale));
    };

    if (spec.bars)
    {
        // Each sample's bar extends to the next sample so bars butt against each other.
        bool hasHeld = false;
        unsigned long long held = 0;
        for (int column = 0; column <= plotWidth; ++column)
        {
            if (ranges[column].hasValue)
            {
                held = ranges[column].maximum;
                hasHeld = true;
            }
            if (!hasHeld)
            {
                continue;
            }
            const int barTop = std::min(valueToY(held), bottom - 1);
            FillRectColor(dc, {left + column, barTop, left + column + 1, bottom}, spec.color);
        }
    }
    else
    {
        HPEN linePen = CreatePen(PS_SOLID, 2, spec.color);
        previousPen = SelectObject(dc, linePen);
        POINT previousCenter{};
        bool hasPreviousCenter = false;
        for (int bucket = 0; bucket <= plotWidth; ++bucket)
        {
            const PixelRange& range = ranges[bucket];
            if (!range.hasValue)
            {
                continue;
            }
            const int highY = valueToY(range.maximum);
            const int lowY = valueToY(range.minimum);
            const POINT center{left + bucket, (highY + lowY) / 2};
            if (hasPreviousCenter)
            {
                MoveToEx(dc, previousCenter.x, previousCenter.y, nullptr);
                LineTo(dc, center.x, center.y);
            }
            MoveToEx(dc, left + bucket, highY, nullptr);
            LineTo(dc, left + bucket, lowY == highY ? lowY + 1 : lowY);
            previousCenter = center;
            hasPreviousCenter = true;
        }
        SelectObject(dc, previousPen);
        DeleteObject(linePen);
    }

    const auto textWidth = [&](const std::wstring& text)
    {
        const HGDIOBJ previousFont = SelectObject(dc, fonts.body);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, previousFont);
        return static_cast<int>(size.cx);
    };

    if (spec.showAxis)
    {
        const auto drawAxisLabel = [&](const std::wstring& text, bool alignRight)
        {
            const int width = textWidth(text);
            const int x = alignRight ? right - width : left;
            const RECT textBounds{x, bounds.bottom - 19, x + width, bounds.bottom};
            DrawText(dc, text, textBounds, fonts.body, kMutedText);
            if (view.labelHits)
            {
                view.labelHits->push_back(textBounds);
            }
        };
        drawAxisLabel(view.leftLabel, false);
        drawAxisLabel(view.rightLabel, true);
    }

    if (!view.hasMouse || view.mouse.x < left || view.mouse.x > right)
    {
        return;
    }

    const int x = view.mouse.x;
    HPEN hoverPen = CreatePen(PS_SOLID, 1, kMutedText);
    previousPen = SelectObject(dc, hoverPen);
    MoveToEx(dc, x, top, nullptr);
    LineTo(dc, x, bottom + 1);
    SelectObject(dc, previousPen);
    DeleteObject(hoverPen);

    const ULONGLONG hoverTime = windowStart + windowMs * static_cast<ULONGLONG>(x - left) / static_cast<ULONGLONG>(plotWidth);
    double fraction = -1.0;
    for (int offset = 0;; ++offset)
    {
        ULONGLONG timestampMs = 0;
        double value = 0;
        if (!sampleAt(offset, timestampMs, value))
        {
            break;
        }
        if (timestampMs <= hoverTime)
        {
            fraction = value;
            break;
        }
    }

    std::wstring valueText = L"--";
    if (fraction >= 0 && spec.bars)
    {
        valueText = std::to_wstring(static_cast<unsigned long long>(fraction * static_cast<double>(spec.total) / 1048576.0 + 0.5)) + L" MB";
    }
    else if (fraction >= 0)
    {
        valueText = std::to_wstring(static_cast<int>(fraction * 100.0 + 0.5)) + L"%";
    }

    const auto drawHoverBox = [&](const std::wstring& text, int boxLeft, int boxTop, COLORREF color)
    {
        const RECT box{boxLeft, boxTop, boxLeft + textWidth(text) + 12, boxTop + 17};
        FillRectColor(dc, box, kCardBackground);
        HBRUSH border = CreateSolidBrush(color);
        FrameRect(dc, &box, border);
        DeleteObject(border);
        DrawText(dc, text, box, fonts.body, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    const int valueWidth = textWidth(valueText) + 12;
    drawHoverBox(valueText, x + 6 + valueWidth > right ? x - 6 - valueWidth : x + 6, top + 2, spec.color);

    if (spec.bars)
    {
        return;
    }
    const std::wstring timeText = FormatClock(view.now, hoverTime);
    const int timeWidth = textWidth(timeText) + 12;
    drawHoverBox(timeText, std::clamp(x - timeWidth / 2, static_cast<int>(bounds.left) + 2, static_cast<int>(bounds.right) - 2 - timeWidth), bounds.top + 2, kTimeText);
}

std::wstring FormatMemory(unsigned long long bytes)
{
    const unsigned long long tenthsOfGigabytes =
        (bytes * 10 + (1ULL << 29)) / (1ULL << 30);
    return std::to_wstring(tenthsOfGigabytes / 10) + L"." + std::to_wstring(tenthsOfGigabytes % 10) + L" GB";
}

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

// fraction maps a sample to 0..1, or a negative value when the sample is invalid.
template <typename T, typename Fraction>
SampleAt MakeSampler(const std::vector<T>& history, int next, int count, Fraction fraction)
{
    return [&history, next, count, fraction](int offset, ULONGLONG& timestampMs, double& value)
    {
        const T* sample = HistoryAt(history, next, count, offset);
        if (!sample)
        {
            return false;
        }
        timestampMs = sample->timestampMs;
        value = fraction(*sample);
        return true;
    };
}

ChartSpec MakeLineChart(std::wstring title)
{
    ChartSpec chart;
    chart.title = std::move(title);
    chart.color = kAccent;
    chart.topLabel = L"100%";
    chart.midLabel = L"50%";
    chart.bottomLabel = L"0%";
    return chart;
}

ChartSpec MakeMemoryChart(std::wstring title, unsigned long long total)
{
    ChartSpec chart;
    chart.title = std::move(title);
    chart.color = kMemory;
    chart.topLabel = total ? FormatMemory(total) : L"--";
    chart.midLabel = total ? FormatMemory(total / 2) : L"--";
    chart.bottomLabel = L"0 GB";
    chart.total = total;
    chart.bars = true;
    return chart;
}

struct MetricSlot
{
    int labelTop;
    int valueTop;
    int valueBottom;
};

constexpr MetricSlot kLoadSlot{49, 65, 116};
constexpr MetricSlot kTemperatureSlot{119, 138, 166};
constexpr MetricSlot kMemorySlot{178, 198, 232};

struct PanelMetric
{
    std::wstring label;
    std::wstring value;
    COLORREF color;
    bool large;
    MetricSlot slot;
};

// Everything a GPU card or the system panel needs to render; the same code draws both.
struct PanelSpec
{
    std::wstring title;
    std::wstring headerInfo;
    std::wstring collapsedInfo;
    bool collapsed = false;
    std::vector<PanelMetric> metrics;
    ChartSpec charts[2];
    SampleAt samplers[2];
};

void DrawChevron(HDC dc, int x, int midY, bool collapsed)
{
    POINT arrow[3];
    if (collapsed)
    {
        arrow[0] = {x - 3, midY - 5};
        arrow[1] = {x - 3, midY + 5};
        arrow[2] = {x + 3, midY};
    }
    else
    {
        arrow[0] = {x - 5, midY - 3};
        arrow[1] = {x + 5, midY - 3};
        arrow[2] = {x, midY + 3};
    }
    HPEN pen = CreatePen(PS_SOLID, 1, kMutedText);
    HBRUSH brush = CreateSolidBrush(kMutedText);
    const HGDIOBJ previousPen = SelectObject(dc, pen);
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    Polygon(dc, arrow, 3);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawPanel(HDC dc, const PanelSpec& spec, RECT panel,
    const Fonts& fonts, const TimeView& view)
{
    FillRectColor(dc, panel, kCardBackground);
    DrawChevron(dc, panel.left + 22, panel.top + kPanelHeaderHeight / 2, spec.collapsed);

    DrawText(dc, spec.title, {panel.left + 38, panel.top, panel.left + 340, panel.top + kPanelHeaderHeight}, fonts.heading, kPrimaryText);
    DrawText(dc, spec.collapsed ? spec.collapsedInfo : spec.headerInfo,
        {panel.left + 350, panel.top, panel.right - 18, panel.top + kPanelHeaderHeight},
        fonts.body, kMutedText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (spec.collapsed)
    {
        return;
    }

    for (const PanelMetric& metric : spec.metrics)
    {
        DrawText(dc, metric.label, {panel.left + 20, panel.top + metric.slot.labelTop, panel.left + 180, panel.top + metric.slot.labelTop + 21},
            fonts.body, kMutedText);
        DrawText(dc, metric.value,
            {panel.left + (metric.large ? 18 : 20), panel.top + metric.slot.valueTop, panel.left + 188, panel.top + metric.slot.valueBottom},
            metric.large ? fonts.metric : fonts.heading, metric.color);
    }

    const RECT area{panel.left + 198, panel.top + 48, panel.right - 16, panel.bottom - 12};
    const int middle = (area.top + area.bottom) / 2;
    ChartSpec upper = spec.charts[0];
    upper.showAxis = false;
    DrawChart(dc, {area.left, area.top, area.right, middle - 5}, upper, spec.samplers[0], fonts, view);
    DrawChart(dc, {area.left, middle + 5, area.right, area.bottom}, spec.charts[1], spec.samplers[1], fonts, view);
}

void DrawGpuCard(HDC dc, const GpuInfo& gpu, RECT card, const Fonts& fonts,
    const TimeView& view, bool collapsed)
{
    const Sample* latest = HistoryAt(gpu.history, gpu.historyNext, gpu.historyCount, 0);
    const bool memoryValid = latest && latest->memoryValid;
    const std::wstring loadText = gpu.loadValid ? std::to_wstring(gpu.load) + L"%" : L"--";
    const std::wstring temperatureText = gpu.temperatureValid ? std::to_wstring(gpu.temperature) + L" C" : L"--";
    const std::wstring memoryText = memoryValid ? FormatMemory(latest->memoryUsed) : L"--";

    PanelSpec spec;
    spec.title = gpu.name;
    if (gpu.driverModeValid)
    {
        spec.title += L" (" + gpu.driverMode + L")";
    }
    spec.collapsedInfo = L"GPU " + loadText + L"  |  VRAM " + memoryText;
    spec.collapsed = collapsed;
    spec.metrics = {
        {L"GPU LOAD", loadText, gpu.loadValid ? kAccent : kWarning, true, kLoadSlot},
        {L"TEMPERATURE", temperatureText, gpu.temperatureValid ? kTemperature : kWarning, false, kTemperatureSlot},
        {L"MEMORY USAGE", memoryText, memoryValid ? kMemory : kWarning, false, kMemorySlot},
    };

    spec.charts[0] = MakeLineChart(L"GPU LOAD");
    spec.samplers[0] = MakeSampler(gpu.history, gpu.historyNext, gpu.historyCount,
        [](const Sample& sample)
        {
            return sample.valid ? sample.load / 100.0 : -1.0;
        });

    const unsigned long long total = gpu.memoryTotal;
    spec.charts[1] = MakeMemoryChart(L"VRAM", total);
    spec.samplers[1] = MakeSampler(gpu.history, gpu.historyNext, gpu.historyCount,
        [total](const Sample& sample)
        {
            return sample.memoryValid && total ? static_cast<double>(sample.memoryUsed) / static_cast<double>(total) : -1.0;
        });

    DrawPanel(dc, spec, card, fonts, view);
}

void DrawCombinedVramRow(HDC dc, const AppState& state, RECT bounds, const Fonts& fonts)
{
    FillRectColor(dc, bounds, kCardBackground);
    const bool collapsed = state.combinedCollapsed;
    DrawChevron(dc, bounds.left + 22, bounds.top + kPanelHeaderHeight / 2, collapsed);

    RECT labelBounds{bounds.left + 38, bounds.top, bounds.left + 200,
        bounds.top + kPanelHeaderHeight};
    DrawText(dc, L"COMBINED VRAM", labelBounds, fonts.body, kMutedText);

    unsigned long long used = 0;
    unsigned long long total = 0;
    bool valid = true;
    for (const GpuInfo& gpu : state.gpus)
    {
        if (gpu.historyCount == 0)
        {
            valid = false;
            break;
        }
        const Sample& latestSample = gpu.history[(gpu.historyNext - 1 + kHistoryCapacity) % kHistoryCapacity];
        if (!latestSample.memoryValid)
        {
            valid = false;
            break;
        }
        used += latestSample.memoryUsed;
        total += latestSample.memoryTotal;
    }

    const std::wstring memoryText = valid
                                        ? FormatMemory(used) + L" / " + FormatMemory(total)
                                        : L"-- / --";
    if (collapsed)
    {
        RECT collapsedValue{bounds.left + 200, bounds.top, bounds.right - 18,
            bounds.top + kPanelHeaderHeight};
        DrawText(dc, memoryText, collapsedValue, fonts.heading, valid ? kMemory : kWarning);
        return;
    }
    RECT valueBounds{bounds.left + 38, bounds.top + 35, bounds.left + 330,
        bounds.bottom - 10};
    DrawText(dc, memoryText, valueBounds, fonts.heading, valid ? kMemory : kWarning);

    RECT barBounds{bounds.left + 350, bounds.top + 40, bounds.right - 74, bounds.top + 54};
    FillRectColor(dc, barBounds, kPlotBackground);
    if (valid && total > 0)
    {
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

int SystemPanelHeight(const AppState& state)
{
    return state.systemCollapsed ? kPanelHeaderHeight : kSystemPanelExpandedHeight;
}

bool IsGpuCollapsed(const AppState& state, size_t index)
{
    return index < 32 && ((state.gpuCollapsedMask >> index) & 1U) != 0;
}

struct Layout
{
    std::vector<int> cardTops;
    std::vector<int> cardHeights;
    int combinedTop = 0;
    int combinedHeight = 0;
    int systemTop = 0;
    int systemHeight = 0;
    int contentHeight = 0;
};

// Expanded cards share any client height left over after the fixed-size panels.
Layout ComputeLayout(const AppState& state, int clientHeight)
{
    Layout layout;
    if (state.gpus.empty())
    {
        layout.contentHeight = kHeaderHeight + 100;
        return layout;
    }

    const bool hasCombined = state.gpus.size() > 1;
    layout.combinedHeight = state.combinedCollapsed
                                ? kPanelHeaderHeight
                                : kCombinedVramRowHeight;
    int fixed = kHeaderHeight + 12 + 16;
    int expandedCount = 0;
    if (state.systemCollapsed)
    {
        fixed += kPanelHeaderHeight;
    }
    else
    {
        ++expandedCount;
    }
    for (size_t index = 0; index < state.gpus.size(); ++index)
    {
        const bool collapsed = IsGpuCollapsed(state, index);
        fixed += (collapsed ? kPanelHeaderHeight : 0) + kCardGap;
        expandedCount += collapsed ? 0 : 1;
    }
    if (hasCombined)
    {
        fixed += layout.combinedHeight + kCardGap;
    }
    const int share = expandedCount > 0 ? (clientHeight - fixed) / expandedCount : 0;
    const int expandedHeight = std::max(kCardHeight, share);
    layout.systemHeight = state.systemCollapsed
                              ? kPanelHeaderHeight
                              : std::max(kSystemPanelExpandedHeight, share);

    int y = kHeaderHeight + 12;
    for (size_t index = 0; index < state.gpus.size(); ++index)
    {
        const int height = IsGpuCollapsed(state, index) ? kPanelHeaderHeight : expandedHeight;
        layout.cardTops.push_back(y);
        layout.cardHeights.push_back(height);
        y += height + kCardGap;
    }
    if (hasCombined)
    {
        layout.combinedTop = y;
        y += layout.combinedHeight + kCardGap;
    }
    layout.systemTop = y;
    layout.contentHeight = y + layout.systemHeight + 16;
    return layout;
}

int ContentHeight(const AppState& state)
{
    return ComputeLayout(state, 0).contentHeight;
}

void DrawSystemPanel(HDC dc, const AppState& state, RECT panel, const TimeView& view)
{
    const SystemInfo& system = state.system;
    const SystemSample* latest = HistoryAt(system.history, system.historyNext, system.historyCount, 0);
    const bool cpuValid = latest && latest->cpuValid;
    const bool ramValid = latest && latest->ramValid;
    const std::wstring cpuText = cpuValid ? std::to_wstring(latest->cpuPercent) + L"%" : L"--";
    const std::wstring ramText = ramValid ? FormatMemory(latest->ramUsed) : L"--";

    PanelSpec spec;
    spec.title = L"SYSTEM";
    spec.collapsedInfo = L"CPU " + cpuText + L"  |  RAM " + ramText;
    spec.collapsed = state.systemCollapsed;
    spec.metrics = {
        {L"CPU LOAD", cpuText, cpuValid ? kAccent : kWarning, true, kLoadSlot},
        {L"MEMORY USAGE", ramText, ramValid ? kMemory : kWarning, false, kMemorySlot},
    };

    spec.charts[0] = MakeLineChart(L"CPU LOAD");
    spec.samplers[0] = MakeSampler(system.history, system.historyNext, system.historyCount,
        [](const SystemSample& sample)
        {
            return sample.cpuValid ? sample.cpuPercent / 100.0 : -1.0;
        });

    spec.charts[1] = MakeMemoryChart(L"RAM", ramValid ? latest->ramTotal : 0);
    spec.samplers[1] = MakeSampler(system.history, system.historyNext, system.historyCount,
        [](const SystemSample& sample)
        {
            return sample.ramValid ? sample.ramPercent / 100.0 : -1.0;
        });

    DrawPanel(dc, spec, panel, state.fonts, view);
}

ULONGLONG DisplayNow(const AppState& state)
{
    return state.paused ? state.pausedTick : GetTickCount64();
}

ULONGLONG OldestSampleMs(const AppState& state, ULONGLONG now)
{
    ULONGLONG oldest = now;
    const auto consider = [&](const auto& history, int next, int count)
    {
        if (count > 0)
        {
            oldest = std::min(oldest, history[(next - count + kHistoryCapacity) % kHistoryCapacity].timestampMs);
        }
    };
    for (const GpuInfo& gpu : state.gpus)
    {
        consider(gpu.history, gpu.historyNext, gpu.historyCount);
    }
    consider(state.system.history, state.system.historyNext, state.system.historyCount);
    return oldest;
}

struct ScrollGeometry
{
    RECT track{};
    RECT thumb{};
    ULONGLONG now = 0;
    ULONGLONG minEnd = 0;
    ULONGLONG end = 0;
};

// All graphs share one time axis; the thumb positions the view's right edge within recorded history.
ScrollGeometry ComputeScroll(const AppState& state, const RECT& client, ULONGLONG now)
{
    ScrollGeometry geometry;
    geometry.now = now;
    const ULONGLONG windowMs = state.graphWindowMs;
    const ULONGLONG oldest = OldestSampleMs(state, now);
    geometry.minEnd = std::min(now, oldest + windowMs);
    geometry.end = state.followLive ? now : std::clamp(state.viewEndMs, geometry.minEnd, now);

    geometry.track = {18, client.bottom - kScrollStripHeight, client.right - 18, client.bottom};
    const int trackWidth = static_cast<int>(geometry.track.right - geometry.track.left);
    const ULONGLONG span = now - oldest;
    int thumbLength = trackWidth;
    if (span > windowMs)
    {
        thumbLength = std::clamp(static_cast<int>(trackWidth * windowMs / span), 24, trackWidth);
    }
    const ULONGLONG range = now - geometry.minEnd;
    const int left = geometry.track.left + (range > 0
        ? static_cast<int>((trackWidth - thumbLength) * (geometry.end - geometry.minEnd) / range)
        : 0);
    geometry.thumb = {left, geometry.track.top + 4, left + thumbLength, geometry.track.bottom - 4};
    return geometry;
}

// Wall-clock time of a GetTickCount64 timestamp, dated only when it is not today.
std::wstring FormatClock(ULONGLONG nowTick, ULONGLONG tick)
{
    FILETIME utcNow{};
    GetSystemTimeAsFileTime(&utcNow);
    ULARGE_INTEGER value{};
    value.LowPart = utcNow.dwLowDateTime;
    value.HighPart = utcNow.dwHighDateTime;
    ULARGE_INTEGER past{};
    past.QuadPart = value.QuadPart - (nowTick - tick) * 10000ULL;

    const FILETIME utcThen{past.LowPart, past.HighPart};
    FILETIME localThen{};
    FILETIME localNow{};
    FileTimeToLocalFileTime(&utcThen, &localThen);
    FileTimeToLocalFileTime(&utcNow, &localNow);
    SYSTEMTIME then{};
    SYSTEMTIME today{};
    FileTimeToSystemTime(&localThen, &then);
    FileTimeToSystemTime(&localNow, &today);

    wchar_t text[32]{};
    if (then.wDay != today.wDay || then.wMonth != today.wMonth)
    {
        swprintf_s(text, L"%02d-%02d %02d:%02d:%02d", then.wMonth, then.wDay, then.wHour, then.wMinute, then.wSecond);
    }
    else
    {
        swprintf_s(text, L"%02d:%02d:%02d", then.wHour, then.wMinute, then.wSecond);
    }
    return text;
}

TimeView MakeTimeView(const AppState& state, const ScrollGeometry& geometry)
{
    TimeView view;
    view.end = geometry.end;
    view.now = GetTickCount64();
    view.windowMs = state.graphWindowMs;
    view.mouse = state.mouse;
    view.hasMouse = state.mouseInside && !state.draggingScroll;
    if (state.showClockTime)
    {
        view.leftLabel = FormatClock(GetTickCount64(), geometry.end - state.graphWindowMs);
        view.rightLabel = FormatClock(GetTickCount64(), geometry.end);
        return view;
    }
    view.leftLabel = FormatAgo(geometry.now - geometry.end + state.graphWindowMs);
    view.rightLabel = FormatAgo(geometry.now - geometry.end);
    return view;
}

void DragScrollThumb(AppState& state, const ScrollGeometry& geometry, int mouseX)
{
    const int trackWidth = static_cast<int>(geometry.track.right - geometry.track.left);
    const int thumbLength = static_cast<int>(geometry.thumb.right - geometry.thumb.left);
    const int travel = trackWidth - thumbLength;
    const ULONGLONG range = geometry.now - geometry.minEnd;
    const double fraction = travel > 0
        ? std::clamp(static_cast<double>(mouseX - state.dragGrabX - geometry.track.left) / travel, 0.0, 1.0)
        : 1.0;
    state.followLive = range == 0 || fraction >= 0.999;
    state.viewEndMs = geometry.minEnd + static_cast<ULONGLONG>(fraction * static_cast<double>(range));
}

void DrawScrollStrip(HDC dc, const AppState& state, const ScrollGeometry& geometry, const RECT& client)
{
    FillRectColor(dc, {0, geometry.track.top, client.right, client.bottom}, kBackground);
    HBRUSH brush = CreateSolidBrush(state.draggingScroll ? RGB(135, 139, 143) : RGB(98, 102, 106));
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    const HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, geometry.thumb.left, geometry.thumb.top, geometry.thumb.right + 1, geometry.thumb.bottom + 1, 6, 6);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(brush);
}

void PositionHeaderControls(HWND window, const AppState& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int updateX = std::max(24,
        static_cast<int>(client.right) - 24 - kUpdateSpeedComboWidth);
    const int windowX = updateX - kHeaderGroupOffset;
    if (state.updateSpeedCombo)
    {
        MoveWindow(state.updateSpeedCombo, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.updateSpeedButton)
    {
        MoveWindow(state.updateSpeedButton, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
    if (state.windowSizeCombo)
    {
        MoveWindow(state.windowSizeCombo, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.windowSizeButton)
    {
        MoveWindow(state.windowSizeButton, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
}

void OverlapDropdownBorder(HWND combo, HWND button)
{
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    RECT listBounds{};
    RECT buttonBounds{};
    if (!GetComboBoxInfo(combo, &comboInfo) || !comboInfo.hwndList || !GetWindowRect(comboInfo.hwndList, &listBounds) || !GetWindowRect(button, &buttonBounds))
    {
        return;
    }

    int top = listBounds.top;
    int height = listBounds.bottom - listBounds.top;
    if (listBounds.top >= buttonBounds.bottom)
    {
        --top;
        ++height;
    }
    else if (listBounds.bottom <= buttonBounds.top)
    {
        ++height;
    }
    else
    {
        return;
    }

    SetWindowPos(comboInfo.hwndList, nullptr, listBounds.left, top,
        listBounds.right - listBounds.left, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void EnableDarkTitleBar(HWND window)
{
    HMODULE dwmLibrary = LoadLibraryW(L"dwmapi.dll");
    if (dwmLibrary == nullptr)
    {
        return;
    }

    using SetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const auto setWindowAttribute = reinterpret_cast<SetWindowAttributeFunction>(
        GetProcAddress(dwmLibrary, "DwmSetWindowAttribute"));
    if (setWindowAttribute != nullptr)
    {
        const BOOL enabled = TRUE;
        constexpr DWORD kImmersiveDarkMode = 20;
        constexpr DWORD kImmersiveDarkModeLegacy = 19;
        if (FAILED(setWindowAttribute(window, kImmersiveDarkMode, &enabled, sizeof(enabled))))
        {
            setWindowAttribute(window, kImmersiveDarkModeLegacy, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(dwmLibrary);
}

void FitWindowToContent(HWND window, const AppState& state)
{
    RECT client{};
    RECT windowBounds{};
    GetClientRect(window, &client);
    GetWindowRect(window, &windowBounds);

    RECT desiredBounds{0, 0, static_cast<LONG>(client.right),
        std::max(static_cast<int>(client.bottom), ContentHeight(state) + kScrollStripHeight)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&desiredBounds, style, FALSE, extendedStyle))
    {
        return;
    }

    int width = desiredBounds.right - desiredBounds.left;
    int height = desiredBounds.bottom - desiredBounds.top;
    int x = windowBounds.left;
    int y = windowBounds.top;

    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitorInfo))
    {
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

void UpdateScrollBar(HWND window, AppState& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int clientHeight = static_cast<int>(client.bottom - client.top) - kScrollStripHeight;
    const int maxPosition = std::max(0, ContentHeight(state) - clientHeight);
    state.scrollPosition = std::clamp(state.scrollPosition, 0, maxPosition);

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, ContentHeight(state) - 1);
    info.nPage = static_cast<UINT>(clientHeight);
    info.nPos = state.scrollPosition;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void SetScrollPosition(HWND window, AppState& state, int position)
{
    state.scrollPosition = position;
    UpdateScrollBar(window, state);
    InvalidateRect(window, nullptr, FALSE);
}

void PaintWindow(HWND window, AppState& state)
{
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);
    // While paused, draw from the frozen snapshot; collection continues in the live data.
    if (state.paused)
    {
        std::swap(state.gpus, state.pausedGpus);
        std::swap(state.system, state.pausedSystem);
    }

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
    const bool buffered = bufferDc != nullptr && bufferBitmap != nullptr && previousBitmap != nullptr && previousBitmap != HGDI_ERROR;
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
    const COLORREF statusColor = state.nvmlInitialized && !state.paused ? kMutedText : kWarning;
    DrawText(dc, state.paused ? L"PAUSED (space to resume)  |  " + state.status : state.status, statusBounds, state.fonts.body, statusColor);

    if (state.gpus.empty())
    {
        RECT messageBounds{24, kHeaderHeight + 20, client.right - 24, kHeaderHeight + 70};
        DrawText(dc, state.status.empty() ? L"Waiting for NVML" : state.status,
            messageBounds, state.fonts.heading, kPrimaryText);
    }
    else
    {
        const ULONGLONG now = DisplayNow(state);
        const ScrollGeometry scroll = ComputeScroll(state, client, now);
        TimeView view = MakeTimeView(state, scroll);
        state.axisLabelRects.clear();
        view.labelHits = &state.axisLabelRects;
        const Layout layout = ComputeLayout(state, height - kScrollStripHeight);
        for (size_t index = 0; index < state.gpus.size(); ++index)
        {
            const int top = layout.cardTops[index] - state.scrollPosition;
            RECT card{18, top, client.right - 18, top + layout.cardHeights[index]};
            DrawGpuCard(dc, state.gpus[index], card, state.fonts,
                view, IsGpuCollapsed(state, index));
        }
        if (state.gpus.size() > 1)
        {
            const int top = layout.combinedTop - state.scrollPosition;
            RECT summary{18, top, client.right - 18, top + layout.combinedHeight};
            DrawCombinedVramRow(dc, state, summary, state.fonts);
        }
        const int panelTop = layout.systemTop - state.scrollPosition;
        DrawSystemPanel(dc, state,
            {18, panelTop, client.right - 18, panelTop + layout.systemHeight},
            view);
        DrawScrollStrip(dc, state, scroll, client);
    }

    if (buffered)
    {
        BitBlt(targetDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, previousBitmap);
    }
    if (bufferBitmap)
        DeleteObject(bufferBitmap);
    if (bufferDc)
        DeleteDC(bufferDc);
    if (state.paused)
    {
        std::swap(state.gpus, state.pausedGpus);
        std::swap(state.system, state.pausedSystem);
    }
    EndPaint(window, &paint);
}

void DestroyFonts(Fonts& fonts)
{
    if (fonts.title)
        DeleteObject(fonts.title);
    if (fonts.heading)
        DeleteObject(fonts.heading);
    if (fonts.body)
        DeleteObject(fonts.body);
    if (fonts.metric)
        DeleteObject(fonts.metric);
}

void CreateFonts(Fonts& fonts)
{
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

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
        case WM_NCCREATE:
        {
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
                WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateSpeedControlId)),
                GetModuleHandleW(nullptr), nullptr);
            if (state->updateSpeedCombo)
            {
                SendMessageW(state->updateSpeedCombo, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
                for (const SamplingOption& option : kSamplingOptions)
                {
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
                WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowSizeControlId)),
                GetModuleHandleW(nullptr), nullptr);
            if (state->windowSizeCombo)
            {
                SendMessageW(state->windowSizeCombo, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
                for (const WindowSizeOption& option : kWindowSizeOptions)
                {
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
            UpdateScrollBar(window, *state);
            return 0;
        case WM_COMMAND:
            if (state && LOWORD(wParam) == kUpdateSpeedButtonId && HIWORD(wParam) == BN_CLICKED)
            {
                EnableWindow(state->updateSpeedButton, FALSE);
                ShowWindow(state->updateSpeedCombo, SW_SHOW);
                SetWindowPos(state->updateSpeedButton, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetFocus(state->updateSpeedCombo);
                SendMessageW(state->updateSpeedCombo, CB_SHOWDROPDOWN, TRUE, 0);
                OverlapDropdownBorder(state->updateSpeedCombo, state->updateSpeedButton);
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeButtonId && HIWORD(wParam) == BN_CLICKED)
            {
                EnableWindow(state->windowSizeButton, FALSE);
                ShowWindow(state->windowSizeCombo, SW_SHOW);
                SetWindowPos(state->windowSizeButton, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetFocus(state->windowSizeCombo);
                SendMessageW(state->windowSizeCombo, CB_SHOWDROPDOWN, TRUE, 0);
                OverlapDropdownBorder(state->windowSizeCombo, state->windowSizeButton);
                return 0;
            }
            if (state && LOWORD(wParam) == kUpdateSpeedControlId && HIWORD(wParam) == CBN_CLOSEUP)
            {
                ShowWindow(state->updateSpeedCombo, SW_HIDE);
                EnableWindow(state->updateSpeedButton, TRUE);
                InvalidateRect(state->updateSpeedButton, nullptr, FALSE);
                SetFocus(window);
                return 0;
            }
            if (state && LOWORD(wParam) == kUpdateSpeedControlId && HIWORD(wParam) == CBN_SELCHANGE)
            {
                const LRESULT selection = SendMessageW(state->updateSpeedCombo, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection < static_cast<LRESULT>(kSamplingOptions.size()))
                {
                    state->sampleIntervalMs = kSamplingOptions[selection].intervalMs;
                    SaveSettings(*state);
                    SetTimer(window, kTimerId, state->sampleIntervalMs, nullptr);
                    if (!state->gpus.empty())
                    {
                        UpdateGpuStatus(*state);
                    }
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeControlId && HIWORD(wParam) == CBN_CLOSEUP)
            {
                ShowWindow(state->windowSizeCombo, SW_HIDE);
                EnableWindow(state->windowSizeButton, TRUE);
                InvalidateRect(state->windowSizeButton, nullptr, FALSE);
                SetFocus(window);
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeControlId && HIWORD(wParam) == CBN_SELCHANGE)
            {
                const LRESULT selection = SendMessageW(state->windowSizeCombo,
                    CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection < static_cast<LRESULT>(kWindowSizeOptions.size()))
                {
                    state->graphWindowMs = kWindowSizeOptions[selection].durationMs;
                    SaveSettings(*state);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_CTLCOLORLISTBOX:
            if (state && state->dropdownBackground)
            {
                const std::array<HWND, 2> combos{state->updateSpeedCombo, state->windowSizeCombo};
                for (HWND combo : combos)
                {
                    COMBOBOXINFO info{sizeof(info)};
                    if (combo && GetComboBoxInfo(combo, &info) && reinterpret_cast<HWND>(lParam) == info.hwndList)
                    {
                        SetTextColor(reinterpret_cast<HDC>(wParam), kDropdownText);
                        SetBkColor(reinterpret_cast<HDC>(wParam), kDropdownBackground);
                        return reinterpret_cast<LRESULT>(state->dropdownBackground);
                    }
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_DRAWITEM:
            if (state && state->dropdownBackground)
            {
                auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                HWND combo = nullptr;
                if (item->CtlType == ODT_BUTTON && item->CtlID == kUpdateSpeedButtonId)
                {
                    combo = state->updateSpeedCombo;
                }
                else if (item->CtlType == ODT_BUTTON && item->CtlID == kWindowSizeButtonId)
                {
                    combo = state->windowSizeCombo;
                }
                if (combo)
                {
                    FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                    if (state->dropdownBorder)
                    {
                        FrameRect(item->hDC, &item->rcItem, state->dropdownBorder);
                    }

                    wchar_t label[32]{};
                    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                    if (selection >= 0)
                    {
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
                if (item->CtlType == ODT_COMBOBOX && item->CtlID == kUpdateSpeedControlId)
                {
                    combo = state->updateSpeedCombo;
                }
                else if (item->CtlType == ODT_COMBOBOX && item->CtlID == kWindowSizeControlId)
                {
                    combo = state->windowSizeCombo;
                }
                if (combo)
                {
                    FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                    if (item->itemID != static_cast<UINT>(-1))
                    {
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
            if (state && wParam == kTimerId && state->nvmlInitialized)
            {
                Refresh(*state);
                if (!state->paused)
                {
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (state && wParam == VK_SPACE && (lParam & (1 << 30)) == 0 && !state->gpus.empty())
            {
                state->paused = !state->paused;
                if (state->paused)
                {
                    state->pausedTick = GetTickCount64();
                    state->pausedGpus = state->gpus;
                    state->pausedSystem = state->system;
                }
                else
                {
                    state->pausedGpus = {};
                    state->pausedSystem = {};
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_SIZE:
            if (state)
            {
                PositionHeaderControls(window, *state);
                UpdateScrollBar(window, *state);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_VSCROLL:
            if (state)
            {
                SCROLLINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = SIF_ALL;
                GetScrollInfo(window, SB_VERT, &info);
                int position = state->scrollPosition;
                switch (LOWORD(wParam))
                {
                    case SB_LINEUP:
                        position -= 48;
                        break;
                    case SB_LINEDOWN:
                        position += 48;
                        break;
                    case SB_PAGEUP:
                        position -= static_cast<int>(info.nPage);
                        break;
                    case SB_PAGEDOWN:
                        position += static_cast<int>(info.nPage);
                        break;
                    case SB_THUMBTRACK:
                        position = info.nTrackPos;
                        break;
                    case SB_THUMBPOSITION:
                        position = info.nTrackPos;
                        break;
                    case SB_TOP:
                        position = 0;
                        break;
                    case SB_BOTTOM:
                        position = info.nMax;
                        break;
                    default:
                        return 0;
                }
                SetScrollPosition(window, *state, position);
            }
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            if (state)
            {
                if (!state->mouseInside)
                {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                    TrackMouseEvent(&track);
                    state->mouseInside = true;
                }
                state->mouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && !state->gpus.empty())
            {
                RECT client{};
                GetClientRect(window, &client);
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                for (const RECT& labelRect : state->axisLabelRects)
                {
                    if (PtInRect(&labelRect, point))
                    {
                        state->showClockTime = !state->showClockTime;
                        SaveSettings(*state);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                if (point.y >= client.bottom - kScrollStripHeight)
                {
                    const ScrollGeometry scroll = ComputeScroll(*state, client, DisplayNow(*state));
                    state->dragGrabX = PtInRect(&scroll.thumb, point)
                        ? point.x - static_cast<int>(scroll.thumb.left)
                        : static_cast<int>(scroll.thumb.right - scroll.thumb.left) / 2;
                    state->draggingScroll = true;
                    SetCapture(window);
                    DragScrollThumb(*state, scroll, point.x);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                const Layout layout = ComputeLayout(*state, client.bottom - kScrollStripHeight);
                const auto headerHit = [&](int top)
                {
                    const int y = top - state->scrollPosition;
                    const RECT header{18, y, client.right - 18, y + kPanelHeaderHeight};
                    return PtInRect(&header, point) != FALSE;
                };
                bool toggled = true;
                if (headerHit(layout.systemTop))
                {
                    state->systemCollapsed = !state->systemCollapsed;
                }
                else if (state->gpus.size() > 1 && headerHit(layout.combinedTop))
                {
                    state->combinedCollapsed = !state->combinedCollapsed;
                }
                else
                {
                    toggled = false;
                    for (size_t index = 0; index < state->gpus.size() && index < 32; ++index)
                    {
                        if (headerHit(layout.cardTops[index]))
                        {
                            state->gpuCollapsedMask ^= 1U << index;
                            toggled = true;
                            break;
                        }
                    }
                }
                if (toggled)
                {
                    SaveSettings(*state);
                    UpdateScrollBar(window, *state);
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (state)
            {
                state->mouseInside = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (state)
            {
                if (!state->mouseInside)
                {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                    TrackMouseEvent(&track);
                    state->mouseInside = true;
                }
                state->mouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && state->draggingScroll)
            {
                RECT client{};
                GetClientRect(window, &client);
                const ScrollGeometry scroll = ComputeScroll(*state, client, DisplayNow(*state));
                DragScrollThumb(*state, scroll, GET_X_LPARAM(lParam));
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (state && state->draggingScroll)
            {
                state->draggingScroll = false;
                if (message == WM_LBUTTONUP)
                {
                    ReleaseCapture();
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (state)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                SetScrollPosition(window, *state,
                    state->scrollPosition - delta / WHEEL_DELTA * 48);
            }
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = 680;
            limits->ptMinTrackSize.y = 360;
            return 0;
        }
        case WM_PAINT:
            if (state)
                PaintWindow(window, *state);
            else
                return DefWindowProcW(window, message, wParam, lParam);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (state)
            {
                KillTimer(window, kTimerId);
                if (state->nvmlInitialized)
                    nvmlShutdown();
                DestroyFonts(state->fonts);
                if (state->dropdownBackground)
                    DeleteObject(state->dropdownBackground);
                if (state->dropdownBorder)
                    DeleteObject(state->dropdownBorder);
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
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

    if (!RegisterClassExW(&windowClass))
    {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_COMPOSITED, kWindowClass, L"GPU Monitor",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        nullptr, nullptr, instance, &state);
    if (!window)
    {
        return 1;
    }

    FitWindowToContent(window, state);
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetFocus(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
