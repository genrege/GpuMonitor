#pragma once
#include <windows.h>

#include "app_state.h"
#include "chart.h"

struct ScrollGeometry
{
    RECT track{};
    RECT thumb{};
    ULONGLONG now = 0;
    ULONGLONG minEnd = 0;
    ULONGLONG end = 0;
    bool scrollable = false;
};

struct VerticalScroll
{
    RECT track{};
    RECT thumb{};
    int maxPosition = 0;
    bool scrollable = false;
};

ULONGLONG DisplayNow(const AppState& state);
ScrollGeometry ComputeScroll(const AppState& state, const RECT& client, ULONGLONG now);
TimeView MakeTimeView(const AppState& state, const ScrollGeometry& geometry);
void DragScrollThumb(AppState& state, const ScrollGeometry& geometry, int mouseX);
void DrawScrollStrip(HDC dc, const AppState& state, const ScrollGeometry& geometry, const RECT& client);
VerticalScroll ComputeVerticalScroll(const AppState& state, const RECT& client);
void DragVerticalThumb(AppState& state, const VerticalScroll& scroll, int mouseY);
void DrawVerticalStrip(HDC dc, const AppState& state, const VerticalScroll& scroll);
