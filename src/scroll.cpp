#include "scroll.h"

#include <algorithm>

#include "format.h"
#include "layout.h"

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

// All graphs share one time axis; the thumb positions the view's right edge within recorded history.
ScrollGeometry ComputeScroll(const AppState& state, const RECT& client, ULONGLONG now)
{
    ScrollGeometry geometry;
    geometry.now = now;
    const ULONGLONG windowMs = state.graphWindowMs;
    const ULONGLONG oldest = OldestSampleMs(state, now);
    geometry.minEnd = std::min(now, oldest + windowMs);
    geometry.end = state.followLive ? now : std::clamp(state.viewEndMs, geometry.minEnd, now);

    geometry.track = {18, client.bottom - kScrollStripHeight, client.right - kScrollStripHeight - 8, client.bottom};
    const int trackWidth = static_cast<int>(geometry.track.right - geometry.track.left);
    const ULONGLONG span = now - oldest;
    int thumbLength = trackWidth;
    if (span > windowMs)
    {
        geometry.scrollable = true;
        thumbLength = std::clamp(static_cast<int>(trackWidth * windowMs / span), 24, trackWidth);
    }
    const ULONGLONG range = now - geometry.minEnd;
    const int left = geometry.track.left + (range > 0
        ? static_cast<int>((trackWidth - thumbLength) * (geometry.end - geometry.minEnd) / range)
        : 0);
    geometry.thumb = {left, geometry.track.top + 4, left + thumbLength, geometry.track.bottom - 4};
    return geometry;
}

TimeView MakeTimeView(const AppState& state, const ScrollGeometry& geometry)
{
    TimeView view;
    view.end = geometry.end;
    view.now = GetTickCount64();
    view.windowMs = state.graphWindowMs;
    view.mouse = state.mouse;
    view.hasMouse = state.mouseInside && !state.draggingScroll && !state.draggingVScroll &&
                    std::any_of(state.chartRects.begin(), state.chartRects.end(),
                        [&](const RECT& rect) { return PtInRect(&rect, state.mouse) != FALSE; });
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
    if (!geometry.scrollable)
    {
        return;
    }
    HBRUSH brush = CreateSolidBrush(state.draggingScroll ? RGB(135, 139, 143) : RGB(98, 102, 106));
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    const HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, geometry.thumb.left, geometry.thumb.top, geometry.thumb.right + 1, geometry.thumb.bottom + 1, 6, 6);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(brush);
}

VerticalScroll ComputeVerticalScroll(const AppState& state, const RECT& client)
{
    VerticalScroll scroll;
    const int viewHeight = static_cast<int>(client.bottom) - kScrollStripHeight;
    const int contentHeight = ContentHeight(state);
    scroll.maxPosition = std::max(0, contentHeight - viewHeight);
    scroll.scrollable = contentHeight > viewHeight;
    scroll.track = {client.right - kScrollStripHeight, 0, client.right, viewHeight};
    int thumbLength = viewHeight;
    if (contentHeight > viewHeight)
    {
        thumbLength = std::clamp(viewHeight * viewHeight / contentHeight, 24, viewHeight);
    }
    const int top = scroll.maxPosition > 0 ? (viewHeight - thumbLength) * state.scrollPosition / scroll.maxPosition : 0;
    scroll.thumb = {scroll.track.left + 4, top, scroll.track.right - 4, top + thumbLength};
    return scroll;
}

void DragVerticalThumb(AppState& state, const VerticalScroll& scroll, int mouseY)
{
    const int trackHeight = static_cast<int>(scroll.track.bottom - scroll.track.top);
    const int thumbLength = static_cast<int>(scroll.thumb.bottom - scroll.thumb.top);
    const int travel = trackHeight - thumbLength;
    const double fraction = travel > 0 ? std::clamp(static_cast<double>(mouseY - state.dragGrabY) / travel, 0.0, 1.0) : 0.0;
    state.scrollPosition = static_cast<int>(fraction * scroll.maxPosition);
}

void DrawVerticalStrip(HDC dc, const AppState& state, const VerticalScroll& scroll)
{
    FillRectColor(dc, scroll.track, kBackground);
    if (!scroll.scrollable)
    {
        return;
    }
    HBRUSH brush = CreateSolidBrush(state.draggingVScroll ? RGB(135, 139, 143) : RGB(98, 102, 106));
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    const HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, scroll.thumb.left, scroll.thumb.top, scroll.thumb.right + 1, scroll.thumb.bottom + 1, 6, 6);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(brush);
}

