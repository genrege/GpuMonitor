#pragma once
#include <windows.h>

#include <string>

struct Fonts
{
    HFONT title = nullptr;
    HFONT heading = nullptr;
    HFONT body = nullptr;
    HFONT grid = nullptr;
    HFONT chart = nullptr;
    int chartHeight = 12;
    int gridRowHeight = 24;
    HFONT metric = nullptr;
};

void DrawText(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color,
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
void FillRectColor(HDC dc, const RECT& bounds, COLORREF color);
void CreateFonts(Fonts& fonts, int step);
// Rebuilds only the chart and process-grid fonts; step is added to their default pixel heights.
void CreateScaledFonts(Fonts& fonts, int step);
void DestroyFonts(Fonts& fonts);

