#pragma once
#include <windows.h>

#include <array>

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
constexpr int kPanelHeaderHeight = 40;
constexpr int kScrollStripHeight = 18;
constexpr int kExportFileCsvId = 0x0100;
constexpr int kExportFileTsvId = 0x0110;
constexpr int kExportClipboardCsvId = 0x0120;
constexpr int kExportClipboardTsvId = 0x0130;
constexpr int kKeepProcessHistoryId = 0x0140;
constexpr int kZoomInId = 0x0150;
constexpr int kZoomOutId = 0x0160;
constexpr int kZoomResetId = 0x0170;
constexpr size_t kMaxProcessHistory = 86400;
constexpr wchar_t kExportFolderValue[] = L"ExportFolder";
constexpr UINT kClipboardReadyMessage = WM_APP + 1;
constexpr UINT kExportFailedMessage = WM_APP + 2;
constexpr int kSystemPanelExpandedHeight = kCardHeight;
constexpr ULONGLONG kProcessRefreshMs = 1000;
constexpr int kGridTopPad = 6;
constexpr int kGridBottomPad = 12;
constexpr size_t kMaxGridRows = 15;
constexpr int kMinFontStep = -2;
constexpr int kMaxFontStep = 8;

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

constexpr std::array<SamplingOption, 17> kSamplingOptions{{
    {L"20 ms", 20 },
    {L"50 ms", 50 },
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

constexpr std::array<WindowSizeOption, 14> kWindowSizeOptions{{
    { L"10 s", 10000 },
    { L"30 s", 30000 },
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
const COLORREF kSelectedColor = RGB(255, 64, 64);
const COLORREF kRowHoverBackground = RGB(42, 50, 55);
const COLORREF kRowSelectedBackground = RGB(66, 34, 36);

