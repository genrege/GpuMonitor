#include "window_ui.h"

#include <algorithm>
#include <utility>

#include "layout.h"
#include "panels.h"
#include "scroll.h"

void PositionHeaderControls(HWND window, const AppState& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int updateX = std::max(24,
        static_cast<int>(client.right) - 24 - kUpdateSpeedComboWidth);
    const int windowX = updateX - kHeaderGroupOffset;
    if (state.updateSpeedCombo)
    {
        MoveWindow(state.updateSpeedCombo, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.updateSpeedButton)
    {
        MoveWindow(state.updateSpeedButton, updateX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
    if (state.windowSizeCombo)
    {
        MoveWindow(state.windowSizeCombo, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedDropdownHeight, TRUE);
    }
    if (state.windowSizeButton)
    {
        MoveWindow(state.windowSizeButton, windowX, 15, kUpdateSpeedComboWidth,
            kUpdateSpeedComboHeight, TRUE);
    }
}

void OverlapDropdownBorder(HWND combo, HWND button)
{
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    RECT listBounds{};
    RECT buttonBounds{};
    if (!GetComboBoxInfo(combo, &comboInfo) || !comboInfo.hwndList || !GetWindowRect(comboInfo.hwndList, &listBounds) || !GetWindowRect(button, &buttonBounds))
    {
        return;
    }

    int top = listBounds.top;
    int height = listBounds.bottom - listBounds.top;
    if (listBounds.top >= buttonBounds.bottom)
    {
        --top;
        ++height;
    }
    else if (listBounds.bottom <= buttonBounds.top)
    {
        ++height;
    }
    else
    {
        return;
    }

    SetWindowPos(comboInfo.hwndList, nullptr, listBounds.left, top,
        listBounds.right - listBounds.left, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void EnableDarkTitleBar(HWND window)
{
    HMODULE dwmLibrary = LoadLibraryW(L"dwmapi.dll");
    if (dwmLibrary == nullptr)
    {
        return;
    }

    using SetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const auto setWindowAttribute = reinterpret_cast<SetWindowAttributeFunction>(
        GetProcAddress(dwmLibrary, "DwmSetWindowAttribute"));
    if (setWindowAttribute != nullptr)
    {
        const BOOL enabled = TRUE;
        constexpr DWORD kImmersiveDarkMode = 20;
        constexpr DWORD kImmersiveDarkModeLegacy = 19;
        if (FAILED(setWindowAttribute(window, kImmersiveDarkMode, &enabled, sizeof(enabled))))
        {
            setWindowAttribute(window, kImmersiveDarkModeLegacy, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(dwmLibrary);
}

void FitWindowToContent(HWND window, const AppState& state)
{
    RECT client{};
    RECT windowBounds{};
    GetClientRect(window, &client);
    GetWindowRect(window, &windowBounds);

    RECT desiredBounds{0, 0, static_cast<LONG>(client.right),
        std::max(static_cast<int>(client.bottom), ContentHeight(state) + kScrollStripHeight)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&desiredBounds, style, FALSE, extendedStyle))
    {
        return;
    }

    int width = desiredBounds.right - desiredBounds.left;
    int height = desiredBounds.bottom - desiredBounds.top;
    int x = windowBounds.left;
    int y = windowBounds.top;

    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitorInfo))
    {
        const RECT& workArea = monitorInfo.rcWork;
        width = std::min(width, static_cast<int>(workArea.right - workArea.left));
        height = std::min(height, static_cast<int>(workArea.bottom - workArea.top));
        x = std::clamp(x, static_cast<int>(workArea.left),
            static_cast<int>(workArea.right) - width);
        y = std::clamp(y, static_cast<int>(workArea.top),
            static_cast<int>(workArea.bottom) - height);
    }

    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void UpdateScrollBar(HWND window, AppState& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int clientHeight = static_cast<int>(client.bottom - client.top) - kScrollStripHeight;
    const int maxPosition = std::max(0, ContentHeight(state) - clientHeight);
    state.scrollPosition = std::clamp(state.scrollPosition, 0, maxPosition);
}

void SetScrollPosition(HWND window, AppState& state, int position)
{
    state.scrollPosition = position;
    UpdateScrollBar(window, state);
    InvalidateRect(window, nullptr, FALSE);
}

void PaintWindow(HWND window, AppState& state)
{
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);
    // While paused, draw from the frozen snapshot; collection continues in the live data.
    if (state.paused)
    {
        std::swap(state.gpus, state.pausedGpus);
        std::swap(state.system, state.pausedSystem);
    }

    RECT client{};
    GetClientRect(window, &client);
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);
    HDC bufferDc = CreateCompatibleDC(targetDc);
    HBITMAP bufferBitmap = width > 0 && height > 0
                               ? CreateCompatibleBitmap(targetDc, width, height)
                               : nullptr;
    HGDIOBJ previousBitmap = bufferDc && bufferBitmap
                                 ? SelectObject(bufferDc, bufferBitmap)
                                 : nullptr;
    const bool buffered = bufferDc != nullptr && bufferBitmap != nullptr && previousBitmap != nullptr && previousBitmap != HGDI_ERROR;
    HDC dc = buffered ? bufferDc : targetDc;

    FillRectColor(dc, client, kBackground);

    const int updateComboX = std::max(24,
        static_cast<int>(client.right) - 24 - kUpdateSpeedComboWidth);
    const int windowComboX = updateComboX - kHeaderGroupOffset;
    RECT windowSizeLabel{windowComboX - 76, 15, windowComboX - 8, 42};
    DrawText(dc, L"WINDOW", windowSizeLabel, state.fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT updateLabel{updateComboX - 76, 15, updateComboX - 8, 42};
    DrawText(dc, L"UPDATE", updateLabel, state.fonts.body, kMutedText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT statusBounds{26, 15, windowComboX - 88, 42};
    const COLORREF statusColor = state.providersReady && !state.paused ? kMutedText : kWarning;
    DrawText(dc, state.paused ? L"PAUSED (space to resume)  |  " + state.status : state.status, statusBounds, state.fonts.body, statusColor);

    if (state.gpus.empty())
    {
        RECT messageBounds{24, kHeaderHeight + 20, client.right - 24, kHeaderHeight + 70};
        DrawText(dc, state.status.empty() ? L"Waiting for GPU data" : state.status,
            messageBounds, state.fonts.heading, kPrimaryText);
    }
    else
    {
        const ULONGLONG now = DisplayNow(state);
        const ScrollGeometry scroll = ComputeScroll(state, client, now);
        TimeView view = MakeTimeView(state, scroll);
        state.axisLabelRects.clear();
        view.labelHits = &state.axisLabelRects;
        state.chartRects.clear();
        view.chartHits = &state.chartRects;
        const Layout layout = ComputeLayout(state, height - kScrollStripHeight);
        state.processHeaderHits.clear();
        state.processRowHits.clear();
        state.gridScrollHits.clear();
        GridOptions grid{state.processSortColumn, state.processSortAscending, &state.processHeaderHits,
            &state.processRowHits, &state.gridScrollHits, state.mouseInside, state.mouse};
        const auto gridFor = [&](size_t panel)
        {
            GridOptions options = grid;
            options.panel = panel;
            options.selection = panel < state.selections.size() ? &state.selections[panel] : nullptr;
            return options;
        };
        for (size_t index = 0; index < state.gpus.size(); ++index)
        {
            const int top = layout.cardTops[index] - state.scrollPosition;
            RECT card{18, top, client.right - kScrollStripHeight - 8, top + layout.cardHeights[index]};
            DrawGpuCard(dc, state.gpus[index], card, state.fonts,
                view, GpuView(state, index), gridFor(index));
        }
        if (state.gpus.size() > 1)
        {
            const int top = layout.combinedTop - state.scrollPosition;
            RECT summary{18, top, client.right - kScrollStripHeight - 8, top + layout.combinedHeight};
            DrawCombinedPanel(dc, state, summary, view, gridFor(state.gpus.size()));
        }
        const int panelTop = layout.systemTop - state.scrollPosition;
        DrawSystemPanel(dc, state,
            {18, panelTop, client.right - kScrollStripHeight - 8, panelTop + layout.systemHeight},
            view, gridFor(state.gpus.size() + 1));
        DrawScrollStrip(dc, state, scroll, client);
        DrawVerticalStrip(dc, state, ComputeVerticalScroll(state, client));
    }

    if (buffered)
    {
        BitBlt(targetDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, previousBitmap);
    }
    if (bufferBitmap)
        DeleteObject(bufferBitmap);
    if (bufferDc)
        DeleteDC(bufferDc);
    if (state.paused)
    {
        std::swap(state.gpus, state.pausedGpus);
        std::swap(state.system, state.pausedSystem);
    }
    EndPaint(window, &paint);
}

