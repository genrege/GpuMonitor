#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "draw_util.h"
#include "gpu_provider.h"
#include "model.h"

// Per-panel display state, cycled by clicking the panel header.
enum PanelView
{
    kViewCharts = 0,
    kViewChartsAndGrid = 1,
    kViewGrid = 2,
    kViewCollapsed = 3,
    kViewCount = 4,
};
constexpr size_t kMaxViewGpus = 16;

struct ProcessHeaderHit
{
    RECT bounds;
    int column;
};

struct ProcessRowHit
{
    RECT bounds;
    size_t panel;
    unsigned long pid;
};

struct GridScrollHit
{
    RECT body;
    RECT track;
    size_t panel;
    int total;
    int visible;
};

struct SelectedProcessSample
{
    ULONGLONG timestampMs = 0;
    double load = 0;
    unsigned long long memory = 0;
    bool loadValid = false;
    bool memoryValid = false;
};

// The process highlighted in a panel's grid, plus per-pid history recorded for that panel.
struct PanelSelection
{
    bool active = false;
    unsigned long pid = 0;
    int gridScroll = 0;
    std::unordered_map<unsigned long, std::vector<SelectedProcessSample>> histories;
};

struct AppState
{
    std::vector<GpuInfo> gpus;
    SystemInfo system;
    unsigned int gpuViewModes = 0;
    int combinedView = kViewCharts;
    int systemView = kViewCharts;
    int processSortColumn = kColumnName;
    bool processSortAscending = true;
    ULONGLONG lastProcessRefreshMs = 0;
    // When off, history is recorded only for the selected process.
    bool keepProcessHistory = false;
    int fontStep = 0;
    std::vector<ProcessHeaderHit> processHeaderHits;
    std::vector<ProcessRowHit> processRowHits;
    std::vector<GridScrollHit> gridScrollHits;
    size_t draggingGridPanel = static_cast<size_t>(-1);
    // One per GPU, then the combined panel, then the system panel.
    std::vector<PanelSelection> selections;
    std::wstring status;
    HWND updateSpeedCombo = nullptr;
    HWND updateSpeedButton = nullptr;
    HWND windowSizeCombo = nullptr;
    HWND windowSizeButton = nullptr;
    HBRUSH dropdownBackground = nullptr;
    HBRUSH dropdownBorder = nullptr;
    int sampleIntervalMs = kSampleIntervalMs;
    unsigned int graphWindowMs = 60000;
    bool providersReady = false;
    std::vector<std::unique_ptr<IGpuProvider>> providers;
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
    std::vector<RECT> chartRects;
    ULONGLONG viewEndMs = 0;
    bool draggingScroll = false;
    int dragGrabX = 0;
    bool draggingVScroll = false;
    int dragGrabY = 0;
    Fonts fonts;
};

