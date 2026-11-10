#pragma once
#include <windows.h>

#include "app_state.h"
#include "chart.h"

struct GridOptions
{
    int sortColumn = kColumnName;
    bool ascending = true;
    std::vector<ProcessHeaderHit>* hits = nullptr;
    std::vector<ProcessRowHit>* rowHits = nullptr;
    std::vector<GridScrollHit>* scrollHits = nullptr;
    bool hasMouse = false;
    POINT mouse{};
    size_t panel = 0;
    const PanelSelection* selection = nullptr;
};

void DrawGpuCard(HDC dc, const GpuInfo& gpu, RECT card, const Fonts& fonts,
    const TimeView& view, int panelView, const GridOptions& options);
void DrawCombinedPanel(HDC dc, const AppState& state, RECT bounds, const TimeView& view, const GridOptions& options);
void DrawSystemPanel(HDC dc, const AppState& state, RECT panel, const TimeView& view, const GridOptions& options);
