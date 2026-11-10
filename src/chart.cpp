#include "chart.h"

#include <algorithm>
#include <limits>

#include "format.h"

namespace
{

struct PixelRange
{
    unsigned long long minimum = std::numeric_limits<unsigned long long>::max();
    unsigned long long maximum = 0;
    bool hasValue = false;
    bool gapAfter = false;
};

} // namespace

void DrawChart(HDC dc, RECT bounds, const ChartSpec& spec, const SampleAt& sampleAt,
    const Fonts& panelFonts, const TimeView& view, const SampleAt& overlay)
{
    // Chart text uses its own scalable font; margins scale with it.
    Fonts fonts = panelFonts;
    fonts.body = panelFonts.chart;
    const auto s = [&](int pixels) { return pixels * fonts.chartHeight / 12; };
    const ULONGLONG now = view.end;
    const ULONGLONG windowMs = view.windowMs;
    FillRectColor(dc, bounds, kPlotBackground);
    if (view.chartHits)
    {
        view.chartHits->push_back(bounds);
    }
    HBRUSH frameBrush = CreateSolidBrush(kGridColor);
    FrameRect(dc, &bounds, frameBrush);
    DeleteObject(frameBrush);

    const int left = bounds.left + s(62);
    const int right = bounds.right - 12;
    const int top = bounds.top + s(20);
    const int bottom = bounds.bottom - s(20);
    if (right <= left || bottom <= top)
    {
        return;
    }

    DrawText(dc, spec.title, {bounds.left + 6, bounds.top + 1, right, bounds.top + s(17)},
        fonts.body, spec.color);

    HPEN gridPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ previousPen = SelectObject(dc, gridPen);
    for (int division = 0; division <= 4; ++division)
    {
        const int y = top + (bottom - top) * division / 4;
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, right, y);
    }
    SelectObject(dc, previousPen);
    DeleteObject(gridPen);

    const UINT labelFormat = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
    DrawText(dc, spec.topLabel, {bounds.left + 2, top - s(8), left - 4, top + s(8)},
        fonts.body, kMutedText, labelFormat);
    DrawText(dc, spec.midLabel, {bounds.left + 2, (top + bottom) / 2 - s(8), left - 4, (top + bottom) / 2 + s(8)}, fonts.body, kMutedText, labelFormat);
    DrawText(dc, spec.bottomLabel, {bounds.left + 2, bottom - s(8), left - 4, bottom + s(8)},
        fonts.body, kMutedText, labelFormat);

    constexpr double kScale = 10000.0;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    // Signed: when uptime is shorter than the window the start is negative, not clamped to 0.
    const long long windowStart = static_cast<long long>(now) - static_cast<long long>(windowMs);
    const auto drawSeries = [&](const SampleAt& series, COLORREF color, bool bars)
    {
    std::vector<PixelRange> ranges(static_cast<size_t>(plotWidth + 1));
    const auto addValue = [](PixelRange& range, unsigned long long value)
    {
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
        range.hasValue = true;
    };

    bool hasNewest = false;
    unsigned long long newestValue = 0;
    int newestBucket = 0;
    ULONGLONG newestTimestamp = 0;
    ULONGLONG typicalIntervalMs = 0;
    ULONGLONG previousTimestamp = 0;
    int previousBucket = 0;
    // A recording gap (e.g. sleep) is much longer than the usual sample spacing.
    const auto isGap = [&](ULONGLONG deltaMs)
    {
        return deltaMs > 10000 && deltaMs > 4 * typicalIntervalMs;
    };
    for (int offset = 0;; ++offset)
    {
        ULONGLONG timestampMs = 0;
        double fraction = 0;
        if (!series(offset, timestampMs, fraction) || static_cast<long long>(timestampMs) < windowStart)
        {
            break;
        }
        if (timestampMs > now || fraction < 0)
        {
            continue;
        }
        const ULONGLONG elapsedMs = std::min(static_cast<ULONGLONG>(static_cast<long long>(timestampMs) - windowStart), windowMs);
        const int bucket = static_cast<int>(plotWidth * elapsedMs / windowMs);
        const auto value = static_cast<unsigned long long>(
            std::clamp(fraction, 0.0, 1.0) * kScale);
        addValue(ranges[bucket], value);
        if (!hasNewest)
        {
            hasNewest = true;
            newestValue = value;
            newestBucket = bucket;
            newestTimestamp = timestampMs;
        }
        else
        {
            const ULONGLONG deltaMs = previousTimestamp - timestampMs;
            if (typicalIntervalMs == 0)
            {
                typicalIntervalMs = deltaMs;
            }
            else if (isGap(deltaMs) && bucket != previousBucket)
            {
                ranges[bucket].gapAfter = true;
            }
        }
        previousTimestamp = timestampMs;
        previousBucket = bucket;
    }
    // Extend the newest sample to the right edge so the series reaches "now", unless recording stopped.
    if (hasNewest && newestBucket < plotWidth && !isGap(now - newestTimestamp))
    {
        addValue(ranges[plotWidth], newestValue);
    }

    const auto valueToY = [&](unsigned long long value)
    {
        return bottom - static_cast<int>(plotHeight * value / static_cast<unsigned long long>(kScale));
    };

    if (bars)
    {
        // Each sample's bar extends to the next sample so bars butt against each other.
        bool hasHeld = false;
        unsigned long long held = 0;
        for (int column = 0; column <= plotWidth; ++column)
        {
            if (ranges[column].hasValue)
            {
                held = ranges[column].maximum;
                hasHeld = true;
            }
            if (!hasHeld)
            {
                continue;
            }
            const int barTop = std::min(valueToY(held), bottom - 1);
            FillRectColor(dc, {left + column, barTop, left + column + 1, bottom}, color);
            if (ranges[column].gapAfter)
            {
                hasHeld = false;
            }
        }
    }
    else
    {
        HPEN linePen = CreatePen(PS_SOLID, 2, color);
        previousPen = SelectObject(dc, linePen);
        POINT previousCenter{};
        bool hasPreviousCenter = false;
        for (int bucket = 0; bucket <= plotWidth; ++bucket)
        {
            const PixelRange& range = ranges[bucket];
            if (!range.hasValue)
            {
                continue;
            }
            const int highY = valueToY(range.maximum);
            const int lowY = valueToY(range.minimum);
            const POINT center{left + bucket, (highY + lowY) / 2};
            if (hasPreviousCenter)
            {
                MoveToEx(dc, previousCenter.x, previousCenter.y, nullptr);
                LineTo(dc, center.x, center.y);
            }
            MoveToEx(dc, left + bucket, highY, nullptr);
            LineTo(dc, left + bucket, lowY == highY ? lowY + 1 : lowY);
            previousCenter = center;
            hasPreviousCenter = !range.gapAfter;
        }
        SelectObject(dc, previousPen);
        DeleteObject(linePen);
    }
    };
    drawSeries(sampleAt, spec.color, spec.bars);
    if (overlay)
    {
        drawSeries(overlay, kSelectedColor, spec.bars);
    }

    const auto textWidth = [&](const std::wstring& text)
    {
        const HGDIOBJ previousFont = SelectObject(dc, fonts.body);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, previousFont);
        return static_cast<int>(size.cx);
    };

    if (spec.showAxis)
    {
        const auto drawAxisLabel = [&](const std::wstring& text, bool alignRight)
        {
            const int width = textWidth(text);
            const int x = alignRight ? right - width : left;
            const RECT textBounds{x, bounds.bottom - s(19), x + width, bounds.bottom};
            DrawText(dc, text, textBounds, fonts.body, kMutedText);
            if (view.labelHits)
            {
                view.labelHits->push_back(textBounds);
            }
        };
        drawAxisLabel(view.leftLabel, false);
        drawAxisLabel(view.rightLabel, true);
    }

    if (!view.hasMouse || view.mouse.x < left || view.mouse.x > right)
    {
        return;
    }

    const int x = view.mouse.x;
    HPEN hoverPen = CreatePen(PS_SOLID, 1, kMutedText);
    previousPen = SelectObject(dc, hoverPen);
    MoveToEx(dc, x, top, nullptr);
    LineTo(dc, x, bottom + 1);
    SelectObject(dc, previousPen);
    DeleteObject(hoverPen);

    const ULONGLONG hoverTime = static_cast<ULONGLONG>(windowStart + static_cast<long long>(windowMs * static_cast<ULONGLONG>(x - left) / static_cast<ULONGLONG>(plotWidth)));
    double fraction = -1.0;
    for (int offset = 0;; ++offset)
    {
        ULONGLONG timestampMs = 0;
        double value = 0;
        if (!sampleAt(offset, timestampMs, value))
        {
            break;
        }
        if (timestampMs <= hoverTime)
        {
            fraction = value;
            break;
        }
    }

    std::wstring valueText = L"--";
    if (fraction >= 0 && spec.bars)
    {
        valueText = std::to_wstring(static_cast<unsigned long long>(fraction * static_cast<double>(spec.total) / 1048576.0 + 0.5)) + L" MB";
    }
    else if (fraction >= 0)
    {
        valueText = std::to_wstring(static_cast<int>(fraction * 100.0 + 0.5)) + L"%";
    }

    const auto drawHoverBox = [&](const std::wstring& text, int boxLeft, int boxTop, COLORREF color)
    {
        const RECT box{boxLeft, boxTop, boxLeft + textWidth(text) + 12, boxTop + s(17)};
        FillRectColor(dc, box, kCardBackground);
        HBRUSH border = CreateSolidBrush(color);
        FrameRect(dc, &box, border);
        DeleteObject(border);
        DrawText(dc, text, box, fonts.body, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    const int valueWidth = textWidth(valueText) + 12;
    drawHoverBox(valueText, x + 6 + valueWidth > right ? x - 6 - valueWidth : x + 6, top + 2, spec.color);

    if (spec.bars)
    {
        return;
    }
    const std::wstring timeText = FormatClock(view.now, hoverTime);
    const int timeWidth = textWidth(timeText) + 12;
    drawHoverBox(timeText, std::clamp(x - timeWidth / 2, static_cast<int>(bounds.left) + 2, static_cast<int>(bounds.right) - 2 - timeWidth), bounds.top + 2, kTimeText);
}

ChartSpec MakeLineChart(std::wstring title)
{
    ChartSpec chart;
    chart.title = std::move(title);
    chart.color = kAccent;
    chart.topLabel = L"100%";
    chart.midLabel = L"50%";
    chart.bottomLabel = L"0%";
    return chart;
}

ChartSpec MakeMemoryChart(std::wstring title, unsigned long long total)
{
    ChartSpec chart;
    chart.title = std::move(title);
    chart.color = kMemory;
    chart.topLabel = total ? FormatMemory(total) : L"--";
    chart.midLabel = total ? FormatMemory(total / 2) : L"--";
    chart.bottomLabel = L"0 GB";
    chart.total = total;
    chart.bars = true;
    return chart;
}

