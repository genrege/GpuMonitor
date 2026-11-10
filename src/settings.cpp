#include "settings.h"

#include <algorithm>

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
    if (readFlag(L"GpuViewModes", flag))
        state.gpuViewModes = flag;
    if (readFlag(L"CombinedView", flag) && flag < kViewCount)
        state.combinedView = static_cast<int>(flag);
    if (readFlag(L"SystemView", flag) && flag < kViewCount)
        state.systemView = static_cast<int>(flag);
    if (readFlag(L"ProcessSortColumn", flag) && flag < kColumnCount)
        state.processSortColumn = static_cast<int>(flag);
    if (readFlag(L"ProcessSortAscending", flag))
        state.processSortAscending = flag != 0;
    if (readFlag(L"ShowClockTime", flag))
        state.showClockTime = flag != 0;
    if (readFlag(L"KeepProcessHistory", flag))
        state.keepProcessHistory = flag != 0;
    // Stored as an unsigned DWORD; the step can be negative.
    if (readFlag(L"FontStep", flag))
        state.fontStep = std::clamp(static_cast<int>(flag), kMinFontStep, kMaxFontStep);
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
    writeFlag(L"GpuViewModes", state.gpuViewModes);
    writeFlag(L"CombinedView", static_cast<DWORD>(state.combinedView));
    writeFlag(L"SystemView", static_cast<DWORD>(state.systemView));
    writeFlag(L"ProcessSortColumn", static_cast<DWORD>(state.processSortColumn));
    writeFlag(L"ProcessSortAscending", state.processSortAscending ? 1 : 0);
    writeFlag(L"ShowClockTime", state.showClockTime ? 1 : 0);
    writeFlag(L"KeepProcessHistory", state.keepProcessHistory ? 1 : 0);
    writeFlag(L"FontStep", static_cast<DWORD>(state.fontStep));
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

std::wstring LoadExportFolder()
{
    wchar_t folder[MAX_PATH]{};
    DWORD size = sizeof(folder);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsRegistryPath, kExportFolderValue,
            RRF_RT_REG_SZ, nullptr, folder, &size) != ERROR_SUCCESS)
    {
        return {};
    }
    return folder;
}

void SaveExportFolder(const std::wstring& folder)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsRegistryPath, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }
    RegSetValueExW(key, kExportFolderValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(folder.c_str()),
        static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

