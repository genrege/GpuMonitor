#include "draw_util.h"

void DrawText(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color,
    UINT format)
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

void DestroyFonts(Fonts& fonts)
{
    if (fonts.title)
        DeleteObject(fonts.title);
    if (fonts.heading)
        DeleteObject(fonts.heading);
    if (fonts.body)
        DeleteObject(fonts.body);
    if (fonts.grid)
        DeleteObject(fonts.grid);
    if (fonts.chart)
        DeleteObject(fonts.chart);
    if (fonts.metric)
        DeleteObject(fonts.metric);
}

void CreateScaledFonts(Fonts& fonts, int step)
{
    if (fonts.grid)
        DeleteObject(fonts.grid);
    if (fonts.chart)
        DeleteObject(fonts.chart);
    fonts.chartHeight = 12 + step;
    fonts.gridRowHeight = (15 + step) * 8 / 5;
    fonts.grid = CreateFontW(15 + step, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    fonts.chart = CreateFontW(fonts.chartHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void CreateFonts(Fonts& fonts, int step)
{
    fonts.title = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    fonts.heading = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    fonts.body = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    CreateScaledFonts(fonts, step);
    fonts.metric = CreateFontW(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
}

