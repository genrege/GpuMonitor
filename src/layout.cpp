#include "layout.h"

#include <algorithm>

int GpuView(const AppState& state, size_t index)
{
    return index < kMaxViewGpus ? static_cast<int>((state.gpuViewModes >> (index * 2)) & 3U) : kViewCharts;
}

void SetGpuView(AppState& state, size_t index, int view)
{
    if (index < kMaxViewGpus)
    {
        state.gpuViewModes = (state.gpuViewModes & ~(3U << (index * 2))) | (static_cast<unsigned>(view) << (index * 2));
    }
}

int GridHeight(size_t processCount, int rowHeight)
{
    const size_t rows = std::clamp<size_t>(processCount, 1, kMaxGridRows);
    return kGridTopPad + (rowHeight + 2) + static_cast<int>(rows) * rowHeight + kGridBottomPad;
}

namespace
{
bool ShowsCharts(int view)
{
    return view == kViewCharts || view == kViewChartsAndGrid;
}

bool ShowsGrid(int view)
{
    return view == kViewChartsAndGrid || view == kViewGrid;
}

// Height a panel takes besides its share of the chart area.
int FixedHeight(int view, size_t processCount, int rowHeight)
{
    switch (view)
    {
        case kViewChartsAndGrid:
            return 0;
        case kViewGrid:
            return kPanelHeaderHeight + GridHeight(processCount, rowHeight);
        case kViewCollapsed:
            return kPanelHeaderHeight;
        default:
            return 0;
    }
}
} // namespace

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
    std::vector<const std::vector<ProcessRow>*> lists;
    for (const GpuInfo& gpu : state.gpus)
    {
        lists.push_back(&gpu.processes);
    }
    const size_t combinedCount = hasCombined && state.combinedView != kViewCharts && state.combinedView != kViewCollapsed
                                     ? MergeProcessRows(lists).size()
                                     : 0;

    const int rowHeight = state.fonts.gridRowHeight;
    int fixed = kHeaderHeight + 12 + 16;
    int expandedCount = 0;
    fixed += FixedHeight(state.systemView, state.system.processes.size(), rowHeight);
    expandedCount += ShowsCharts(state.systemView) ? 1 : 0;
    for (size_t index = 0; index < state.gpus.size(); ++index)
    {
        const int view = GpuView(state, index);
        fixed += FixedHeight(view, state.gpus[index].processes.size(), rowHeight) + kCardGap;
        expandedCount += ShowsCharts(view) ? 1 : 0;
    }
    if (hasCombined)
    {
        fixed += kCardGap + FixedHeight(state.combinedView, combinedCount, rowHeight);
        expandedCount += ShowsCharts(state.combinedView) ? 1 : 0;
    }
    const int share = expandedCount > 0 ? (clientHeight - fixed) / expandedCount : 0;
    const int expandedHeight = std::max(kCardHeight, share);
    const auto heightFor = [&](int view, size_t processCount)
    {
        const int chartPart = FixedHeight(view, processCount, rowHeight) + (ShowsCharts(view) ? expandedHeight : 0);
        // Side-by-side grid sits beside the charts, so the panel grows only if the grid is taller.
        return view == kViewChartsAndGrid ? std::max(chartPart, kPanelHeaderHeight + GridHeight(processCount, rowHeight)) : chartPart;
    };
    layout.combinedHeight = hasCombined ? heightFor(state.combinedView, combinedCount) : 0;
    layout.systemHeight = heightFor(state.systemView, state.system.processes.size());

    int y = kHeaderHeight + 12;
    for (size_t index = 0; index < state.gpus.size(); ++index)
    {
        const int height = heightFor(GpuView(state, index), state.gpus[index].processes.size());
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

