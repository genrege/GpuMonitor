#pragma once
#include <vector>

#include "app_state.h"

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

int GpuView(const AppState& state, size_t index);
void SetGpuView(AppState& state, size_t index, int view);
// Height of the process grid block (header row, rows, padding) for a process count.
int GridHeight(size_t processCount, int rowHeight);
Layout ComputeLayout(const AppState& state, int clientHeight);
int ContentHeight(const AppState& state);
