#include "panels.h"

#include <algorithm>

#include "format.h"
#include "layout.h"

struct MetricSlot
{
    int labelTop;
    int valueTop;
    int valueBottom;
};

constexpr MetricSlot kLoadSlot{49, 65, 116};
constexpr MetricSlot kTemperatureSlot{119, 138, 166};
constexpr MetricSlot kMemorySlot{178, 198, 232};

struct PanelMetric
{
    std::wstring label;
    std::wstring value;
    COLORREF color;
    bool large;
    MetricSlot slot;
};

// Everything a GPU card or the system panel needs to render; the same code draws both.
struct PanelSpec
{
    std::wstring title;
    std::wstring collapsedLoad;
    std::wstring collapsedMemory;
    int view = kViewCharts;
    std::vector<ProcessRow> processes;
    std::vector<PanelMetric> metrics;
    ChartSpec charts[2];
    SampleAt samplers[2];
    SampleAt overlays[2];
};

// Overlays the selected process on the charts; only meaningful while the grid is shown beside them.
void AttachSelection(PanelSpec& spec, const GridOptions& options, unsigned long long memoryTotal)
{
    if (!options.selection || !options.selection->active || spec.view != kViewChartsAndGrid)
    {
        return;
    }
    const auto found = options.selection->histories.find(options.selection->pid);
    if (found == options.selection->histories.end())
    {
        return;
    }
    const std::vector<SelectedProcessSample>* history = &found->second;
    const auto sampler = [history](double (*fraction)(const SelectedProcessSample&, unsigned long long), unsigned long long total)
    {
        return [history, fraction, total](int offset, ULONGLONG& timestampMs, double& value)
        {
            if (offset >= static_cast<int>(history->size()))
            {
                return false;
            }
            const SelectedProcessSample& sample = (*history)[history->size() - 1 - offset];
            timestampMs = sample.timestampMs;
            value = fraction(sample, total);
            return true;
        };
    };
    spec.overlays[0] = sampler([](const SelectedProcessSample& sample, unsigned long long)
        { return sample.loadValid ? sample.load / 100.0 : -1.0; }, 0);
    spec.overlays[1] = sampler([](const SelectedProcessSample& sample, unsigned long long total)
        { return sample.memoryValid && total ? static_cast<double>(sample.memory) / static_cast<double>(total) : -1.0; },
        memoryTotal);
}

void DrawChevron(HDC dc, int x, int midY, bool collapsed)
{
    POINT arrow[3];
    if (collapsed)
    {
        arrow[0] = {x - 3, midY - 5};
        arrow[1] = {x - 3, midY + 5};
        arrow[2] = {x + 3, midY};
    }
    else
    {
        arrow[0] = {x - 5, midY - 3};
        arrow[1] = {x + 5, midY - 3};
        arrow[2] = {x, midY + 3};
    }
    HPEN pen = CreatePen(PS_SOLID, 1, kMutedText);
    HBRUSH brush = CreateSolidBrush(kMutedText);
    const HGDIOBJ previousPen = SelectObject(dc, pen);
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    Polygon(dc, arrow, 3);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawProcessGrid(HDC dc, std::vector<ProcessRow> rows, RECT area, const Fonts& fonts,
    const GridOptions& options)
{
    SortProcessRows(rows, options.sortColumn, options.ascending);

    const int rowHeight = fonts.gridRowHeight;
    const int headerHeight = rowHeight + 2;
    const int right = area.right - 20;
    const int memoryLeft = right - 110 * rowHeight / 24;
    const int loadLeft = memoryLeft - 90 * rowHeight / 24;
    const int pidLeft = loadLeft - 90 * rowHeight / 24;
    const int nameLeft = area.left + 20;
    const int columnLeft[kColumnCount] = {nameLeft, pidLeft, loadLeft, memoryLeft};
    const int columnRight[kColumnCount] = {pidLeft - 12, loadLeft - 12, memoryLeft - 12, right};
    const wchar_t* const titles[kColumnCount] = {L"NAME", L"PID", L"LOAD", L"MEMORY"};

    const int headerTop = area.top + kGridTopPad;
    for (int column = 0; column < kColumnCount; ++column)
    {
        const RECT cell{columnLeft[column], headerTop, columnRight[column], headerTop + headerHeight};
        std::wstring title = titles[column];
        if (column == options.sortColumn)
        {
            title += options.ascending ? L" \x25B2" : L" \x25BC";
        }
        DrawText(dc, title, cell, fonts.grid, column == options.sortColumn ? kAccent : kMutedText,
            (column == kColumnName ? DT_LEFT : DT_RIGHT) | DT_VCENTER | DT_SINGLELINE);
        if (options.hits)
        {
            options.hits->push_back({{cell.left - 6, cell.top, cell.right + 6, cell.bottom}, column});
        }
    }
    const RECT separator{nameLeft, headerTop + headerHeight - 1, right, headerTop + headerHeight};
    FillRectColor(dc, separator, kGridColor);

    int y = headerTop + headerHeight;
    if (rows.empty())
    {
        DrawText(dc, L"No processes", {nameLeft, y, right, y + rowHeight}, fonts.grid, kMutedText);
        return;
    }
    const size_t shown = std::min(rows.size(), kMaxGridRows);
    const size_t maxOffset = rows.size() - shown;
    const size_t offset = options.selection ? std::min(static_cast<size_t>(std::max(0, options.selection->gridScroll)), maxOffset) : 0;
    const int bodyTop = y;
    for (size_t index = offset; index < offset + shown; ++index, y += rowHeight)
    {
        const ProcessRow& row = rows[index];
        const RECT rowBounds{area.left + 8, y, area.right - 18, y + rowHeight};
        const bool selected = options.selection && options.selection->active && options.selection->pid == row.pid;
        const bool hovered = options.hasMouse && PtInRect(&rowBounds, options.mouse);
        if (selected || hovered)
        {
            FillRectColor(dc, rowBounds, selected ? kRowSelectedBackground : kRowHoverBackground);
        }
        if (options.rowHits)
        {
            options.rowHits->push_back({rowBounds, options.panel, row.pid});
        }
        std::wstring load = L"--";
        if (row.loadValid)
        {
            wchar_t text[16];
            swprintf_s(text, row.load >= 10.0 ? L"%.0f%%" : L"%.1f%%", row.load);
            load = text;
        }
        const std::wstring cells[kColumnCount] = {
            row.name, std::to_wstring(row.pid), load, row.memoryValid ? FormatMemory(row.memory) : L"--"};
        const COLORREF colors[kColumnCount] = {kPrimaryText, kMutedText, row.loadValid ? kAccent : kMutedText,
            row.memoryValid ? kMemory : kMutedText};
        for (int column = 0; column < kColumnCount; ++column)
        {
            DrawText(dc, cells[column], {columnLeft[column], y, columnRight[column], y + rowHeight}, fonts.grid,
                colors[column], (column == kColumnName ? DT_LEFT : DT_RIGHT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    if (maxOffset > 0)
    {
        const RECT track{area.right - 14, bodyTop, area.right - 8, y};
        FillRectColor(dc, track, kPlotBackground);
        const int trackHeight = track.bottom - track.top;
        const int thumbHeight = std::max(16, static_cast<int>(trackHeight * shown / rows.size()));
        const int thumbTop = track.top + static_cast<int>((trackHeight - thumbHeight) * offset / maxOffset);
        FillRectColor(dc, {track.left, thumbTop, track.right, thumbTop + thumbHeight}, kMutedText);
        if (options.scrollHits)
        {
            options.scrollHits->push_back({{area.left, bodyTop, area.right, y}, track, options.panel,
                static_cast<int>(rows.size()), static_cast<int>(shown)});
        }
    }
}

void DrawPanel(HDC dc, const PanelSpec& spec, RECT panel,
    const Fonts& fonts, const TimeView& view, const GridOptions& options)
{
    FillRectColor(dc, panel, kCardBackground);
    DrawChevron(dc, panel.left + 22, panel.top + kPanelHeaderHeight / 2, spec.view == kViewCollapsed);

    DrawText(dc, spec.title, {panel.left + 38, panel.top, panel.left + 340, panel.top + kPanelHeaderHeight}, fonts.heading, kPrimaryText);
    if (spec.view == kViewGrid || spec.view == kViewCollapsed)
    {
        const HGDIOBJ previousFont = SelectObject(dc, fonts.heading);
        const auto widthOf = [&](const std::wstring& text)
        {
            SIZE size{};
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
            return static_cast<int>(size.cx);
        };
        const int titleWidth = std::min(widthOf(spec.title), 302);
        const int loadWidth = widthOf(spec.collapsedLoad);
        SelectObject(dc, previousFont);

        const int loadLeft = panel.left + 38 + titleWidth + 24;
        const int memoryLeft = loadLeft + loadWidth + 20;
        DrawText(dc, spec.collapsedLoad, {loadLeft, panel.top, memoryLeft, panel.top + kPanelHeaderHeight}, fonts.heading, kAccent);
        DrawText(dc, spec.collapsedMemory, {memoryLeft, panel.top, panel.right - 18, panel.top + kPanelHeaderHeight}, fonts.heading, kMemory);
    }
    if (spec.view == kViewCollapsed)
    {
        return;
    }

    int chartRight = panel.right - 16;
    if (spec.view == kViewGrid)
    {
        DrawProcessGrid(dc, spec.processes, {panel.left, panel.top + kPanelHeaderHeight, panel.right, panel.bottom}, fonts, options);
        return;
    }
    if (spec.view == kViewChartsAndGrid)
    {
        const int available = chartRight - (panel.left + 198);
        const int minGridWidth = 400 * fonts.gridRowHeight / 24;
        const int gridWidth = std::clamp(available / 2, minGridWidth, std::max(minGridWidth, available - 200));
        chartRight -= gridWidth + 16;
        DrawProcessGrid(dc, spec.processes, {chartRight + 16, panel.top + kPanelHeaderHeight, panel.right, panel.bottom}, fonts, options);
    }

    for (const PanelMetric& metric : spec.metrics)
    {
        DrawText(dc, metric.label, {panel.left + 20, panel.top + metric.slot.labelTop, panel.left + 180, panel.top + metric.slot.labelTop + 21},
            fonts.body, kMutedText);
        DrawText(dc, metric.value,
            {panel.left + (metric.large ? 18 : 20), panel.top + metric.slot.valueTop, panel.left + 188, panel.top + metric.slot.valueBottom},
            metric.large ? fonts.metric : fonts.heading, metric.color);
    }

    const RECT area{panel.left + 198, panel.top + 48, chartRight, panel.bottom - 12};
    const int middle = (area.top + area.bottom) / 2;
    ChartSpec upper = spec.charts[0];
    upper.showAxis = false;
    DrawChart(dc, {area.left, area.top, area.right, middle - 5}, upper, spec.samplers[0], fonts, view, spec.overlays[0]);
    DrawChart(dc, {area.left, middle + 5, area.right, area.bottom}, spec.charts[1], spec.samplers[1], fonts, view, spec.overlays[1]);
}

void DrawGpuCard(HDC dc, const GpuInfo& gpu, RECT card, const Fonts& fonts,
    const TimeView& view, int panelView, const GridOptions& options)
{
    const Sample* latest = HistoryAt(gpu.history, gpu.historyNext, gpu.historyCount, 0);
    const bool memoryValid = latest && latest->memoryValid;
    const std::wstring loadText = gpu.loadValid ? std::to_wstring(gpu.load) + L"%" : L"--";
    const std::wstring temperatureText = gpu.temperatureValid ? std::to_wstring(gpu.temperature) + L" C" : L"--";
    const std::wstring memoryText = memoryValid ? FormatMemory(latest->memoryUsed) : L"--";

    PanelSpec spec;
    spec.title = gpu.name;
    if (gpu.driverModeValid)
    {
        spec.title += L" (" + gpu.driverMode + L")";
    }
    spec.collapsedLoad = loadText;
    spec.collapsedMemory = memoryValid ? FormatMegabytes(latest->memoryUsed) : L"--";
    spec.view = panelView;
    spec.processes = gpu.processes;
    spec.metrics = {
        {L"GPU LOAD", loadText, gpu.loadValid ? kAccent : kWarning, true, kLoadSlot},
        {L"TEMPERATURE", temperatureText, gpu.temperatureValid ? kTemperature : kWarning, false, kTemperatureSlot},
        {L"MEMORY USAGE", memoryText, memoryValid ? kMemory : kWarning, false, kMemorySlot},
    };

    spec.charts[0] = MakeLineChart(L"GPU LOAD");
    spec.samplers[0] = MakeSampler(gpu.history, gpu.historyNext, gpu.historyCount,
        [](const Sample& sample)
        {
            return sample.valid ? sample.load / 100.0 : -1.0;
        });

    const unsigned long long total = gpu.memoryTotal;
    spec.charts[1] = MakeMemoryChart(L"VRAM", total);
    spec.samplers[1] = MakeSampler(gpu.history, gpu.historyNext, gpu.historyCount,
        [total](const Sample& sample)
        {
            return sample.memoryValid && total ? static_cast<double>(sample.memoryUsed) / static_cast<double>(total) : -1.0;
        });

    AttachSelection(spec, options, total);
    DrawPanel(dc, spec, card, fonts, view, options);
}

// Combines all GPUs: average load and summed VRAM. GPUs are sampled together, so one history offset is one instant.
void DrawCombinedPanel(HDC dc, const AppState& state, RECT bounds, const TimeView& view, const GridOptions& options)
{
    struct Combined
    {
        ULONGLONG timestampMs = 0;
        double load = 0;
        unsigned long long used = 0;
        unsigned long long total = 0;
        bool loadValid = false;
        bool memoryValid = false;
    };
    const std::vector<GpuInfo>& gpus = state.gpus;
    const auto combinedAt = [&gpus](int offset, Combined& out)
    {
        out = {};
        int loadCount = 0;
        out.memoryValid = true;
        for (const GpuInfo& gpu : gpus)
        {
            const Sample* sample = HistoryAt(gpu.history, gpu.historyNext, gpu.historyCount, offset);
            if (!sample)
            {
                return false;
            }
            out.timestampMs = sample->timestampMs;
            if (sample->valid)
            {
                out.load += std::min(sample->load, 100U);
                ++loadCount;
            }
            out.memoryValid = out.memoryValid && sample->memoryValid;
            out.used += sample->memoryUsed;
            out.total += sample->memoryTotal;
        }
        out.loadValid = loadCount > 0;
        out.load = loadCount > 0 ? out.load / loadCount : 0;
        return true;
    };

    unsigned long long totalVram = 0;
    for (const GpuInfo& gpu : gpus)
    {
        totalVram += gpu.memoryTotal;
    }

    Combined latest;
    const bool hasLatest = combinedAt(0, latest);
    const bool loadValid = hasLatest && latest.loadValid;
    const bool memoryValid = hasLatest && latest.memoryValid;
    const std::wstring loadText = loadValid ? std::to_wstring(static_cast<int>(latest.load + 0.5)) + L"%" : L"--";
    const std::wstring memoryText = memoryValid ? FormatMemory(latest.used) + L" / " + FormatMemory(latest.total) : L"-- / --";

    PanelSpec spec;
    spec.title = L"COMBINED GPU";
    spec.collapsedLoad = loadText;
    spec.collapsedMemory = memoryValid ? FormatMegabytes(latest.used) : L"--";
    spec.view = state.combinedView;
    std::vector<const std::vector<ProcessRow>*> processLists;
    for (const GpuInfo& gpu : gpus)
    {
        processLists.push_back(&gpu.processes);
    }
    spec.processes = MergeProcessRows(processLists);
    spec.metrics = {
        {L"AVERAGE GPU LOAD", loadText, loadValid ? kAccent : kWarning, true, kLoadSlot},
        {L"VRAM USED / TOTAL", memoryText, memoryValid ? kMemory : kWarning, false, kMemorySlot},
    };

    spec.charts[0] = MakeLineChart(L"AVERAGE GPU LOAD");
    spec.samplers[0] = [combinedAt](int offset, ULONGLONG& timestampMs, double& value)
    {
        Combined combined;
        if (!combinedAt(offset, combined))
        {
            return false;
        }
        timestampMs = combined.timestampMs;
        value = combined.loadValid ? combined.load / 100.0 : -1.0;
        return true;
    };

    spec.charts[1] = MakeMemoryChart(L"TOTAL VRAM", totalVram);
    spec.samplers[1] = [combinedAt, totalVram](int offset, ULONGLONG& timestampMs, double& value)
    {
        Combined combined;
        if (!combinedAt(offset, combined))
        {
            return false;
        }
        timestampMs = combined.timestampMs;
        value = combined.memoryValid && totalVram ? static_cast<double>(combined.used) / static_cast<double>(totalVram) : -1.0;
        return true;
    };

    AttachSelection(spec, options, totalVram);
    DrawPanel(dc, spec, bounds, state.fonts, view, options);
}

void DrawSystemPanel(HDC dc, const AppState& state, RECT panel, const TimeView& view, const GridOptions& options)
{
    const SystemInfo& system = state.system;
    const SystemSample* latest = HistoryAt(system.history, system.historyNext, system.historyCount, 0);
    const bool cpuValid = latest && latest->cpuValid;
    const bool ramValid = latest && latest->ramValid;
    const std::wstring cpuText = cpuValid ? std::to_wstring(latest->cpuPercent) + L"%" : L"--";
    const std::wstring ramText = ramValid ? FormatMemory(latest->ramUsed) : L"--";

    PanelSpec spec;
    spec.title = L"SYSTEM";
    spec.collapsedLoad = cpuText;
    spec.collapsedMemory = ramValid ? FormatMegabytes(latest->ramUsed) : L"--";
    spec.view = state.systemView;
    spec.processes = system.processes;
    spec.metrics = {
        {L"CPU LOAD", cpuText, cpuValid ? kAccent : kWarning, true, kLoadSlot},
        {L"MEMORY USAGE", ramText, ramValid ? kMemory : kWarning, false, kMemorySlot},
    };

    spec.charts[0] = MakeLineChart(L"CPU LOAD");
    spec.samplers[0] = MakeSampler(system.history, system.historyNext, system.historyCount,
        [](const SystemSample& sample)
        {
            return sample.cpuValid ? sample.cpuPercent / 100.0 : -1.0;
        });

    spec.charts[1] = MakeMemoryChart(L"RAM", ramValid ? latest->ramTotal : 0);
    spec.samplers[1] = MakeSampler(system.history, system.historyNext, system.historyCount,
        [](const SystemSample& sample)
        {
            return sample.ramValid ? sample.ramPercent / 100.0 : -1.0;
        });

    AttachSelection(spec, options, ramValid ? latest->ramTotal : 0);
    DrawPanel(dc, spec, panel, state.fonts, view, options);
}

