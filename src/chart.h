#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "draw_util.h"
#include "model.h"

// The visible time range: [end - windowMs, end], with axis labels for its two edges.
struct TimeView
{
    ULONGLONG end = 0;
    ULONGLONG windowMs = 0;
    std::wstring leftLabel;
    std::wstring rightLabel;
    ULONGLONG now = 0;
    POINT mouse{};
    bool hasMouse = false;
    std::vector<RECT>* labelHits = nullptr;
    std::vector<RECT>* chartHits = nullptr;
};

struct ChartSpec
{
    std::wstring title;
    COLORREF color = kAccent;
    std::wstring topLabel;
    std::wstring midLabel;
    std::wstring bottomLabel;
    bool bars = false;
    bool showAxis = true;
    unsigned long long total = 0;
};

// Returns false past the oldest sample; fraction is 0..1, or negative when invalid.
using SampleAt = std::function<bool(int offset, ULONGLONG& timestampMs, double& fraction)>;

// fraction maps a sample to 0..1, or a negative value when the sample is invalid.
template <typename T, typename Fraction>
SampleAt MakeSampler(const std::vector<T>& history, int next, int count, Fraction fraction)
{
    return [&history, next, count, fraction](int offset, ULONGLONG& timestampMs, double& value)
    {
        const T* sample = HistoryAt(history, next, count, offset);
        if (!sample)
        {
            return false;
        }
        timestampMs = sample->timestampMs;
        value = fraction(*sample);
        return true;
    };
}

// overlay, when set, is drawn over the main series in the selection colour.
void DrawChart(HDC dc, RECT bounds, const ChartSpec& spec, const SampleAt& sampleAt,
    const Fonts& fonts, const TimeView& view, const SampleAt& overlay = {});
ChartSpec MakeLineChart(std::wstring title);
ChartSpec MakeMemoryChart(std::wstring title, unsigned long long total);

