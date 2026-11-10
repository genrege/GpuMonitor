#include <windows.h>
#include <windowsx.h>

#include <algorithm>

#include "resource.h"

#include "app_state.h"
#include "config.h"
#include "export.h"
#include "layout.h"
#include "sampling.h"
#include "scroll.h"
#include "settings.h"
#include "window_ui.h"

namespace
{
void ApplyZoom(HWND window, AppState& state, int step)
{
    step = std::clamp(step, kMinFontStep, kMaxFontStep);
    if (step == state.fontStep)
    {
        return;
    }
    state.fontStep = step;
    CreateScaledFonts(state.fonts, step);
    SaveSettings(state);
    UpdateScrollBar(window, state);
    InvalidateRect(window, nullptr, FALSE);
}
void ScrollGridTo(AppState& state, const GridScrollHit& hit, int y)
{
    const int maxOffset = hit.total - hit.visible;
    const int trackHeight = std::max(1, static_cast<int>(hit.track.bottom - hit.track.top));
    state.selections[hit.panel].gridScroll =
        std::clamp((y - static_cast<int>(hit.track.top)) * maxOffset / trackHeight, 0, maxOffset);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
        case WM_NCCREATE:
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
            state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            LoadSettings(*state);
            SetWindowTextW(window, L"GPU Monitor");
            EnableDarkTitleBar(window);
            CreateFonts(state->fonts, state->fontStep);
            state->dropdownBackground = CreateSolidBrush(kDropdownBackground);
            state->dropdownBorder = CreateSolidBrush(kDropdownBorder);
            state->updateSpeedCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateSpeedControlId)),
                GetModuleHandleW(nullptr), nullptr);
            if (state->updateSpeedCombo)
            {
                SendMessageW(state->updateSpeedCombo, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
                for (const SamplingOption& option : kSamplingOptions)
                {
                    SendMessageW(state->updateSpeedCombo, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(option.label));
                }
                SendMessageW(state->updateSpeedCombo, CB_SETCURSEL,
                    FindSamplingOption(*state), 0);
                state->updateSpeedButton = CreateWindowExW(0, L"BUTTON", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 15, kUpdateSpeedComboWidth, kUpdateSpeedComboHeight, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateSpeedButtonId)),
                    GetModuleHandleW(nullptr), nullptr);
            }
            state->windowSizeCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                0, 15, kUpdateSpeedComboWidth, kUpdateSpeedDropdownHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowSizeControlId)),
                GetModuleHandleW(nullptr), nullptr);
            if (state->windowSizeCombo)
            {
                SendMessageW(state->windowSizeCombo, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->fonts.body), TRUE);
                for (const WindowSizeOption& option : kWindowSizeOptions)
                {
                    SendMessageW(state->windowSizeCombo, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(option.label));
                }
                SendMessageW(state->windowSizeCombo, CB_SETCURSEL,
                    FindWindowSizeOption(*state), 0);
                state->windowSizeButton = CreateWindowExW(0, L"BUTTON", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 15, kUpdateSpeedComboWidth, kUpdateSpeedComboHeight, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowSizeButtonId)),
                    GetModuleHandleW(nullptr), nullptr);
            }
            PositionHeaderControls(window, *state);
            AddExportMenus(window);
            {
                HMENU systemMenu = GetSystemMenu(window, FALSE);
                HMENU viewMenu = CreatePopupMenu();
                AppendMenuW(viewMenu, MF_STRING, kZoomInId, L"Zoom In\tCtrl++");
                AppendMenuW(viewMenu, MF_STRING, kZoomOutId, L"Zoom Out\tCtrl+-");
                AppendMenuW(viewMenu, MF_STRING, kZoomResetId, L"Reset Zoom\tCtrl+0");
                InsertMenuW(systemMenu, static_cast<UINT>(std::max(0, GetMenuItemCount(systemMenu) - 2)),
                    MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"View");
            }
            InsertMenuW(GetSystemMenu(window, FALSE),
                static_cast<UINT>(std::max(0, GetMenuItemCount(GetSystemMenu(window, FALSE)) - 2)),
                MF_BYPOSITION | MF_STRING | (state->keepProcessHistory ? MF_CHECKED : MF_UNCHECKED),
                kKeepProcessHistoryId, L"Keep history for all processes");
            InitializeProviders(*state);
            SetTimer(window, kTimerId, state->sampleIntervalMs, nullptr);
            UpdateScrollBar(window, *state);
            return 0;
        case WM_COMMAND:
            if (state && LOWORD(wParam) == kUpdateSpeedButtonId && HIWORD(wParam) == BN_CLICKED)
            {
                EnableWindow(state->updateSpeedButton, FALSE);
                ShowWindow(state->updateSpeedCombo, SW_SHOW);
                SetWindowPos(state->updateSpeedButton, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetFocus(state->updateSpeedCombo);
                SendMessageW(state->updateSpeedCombo, CB_SHOWDROPDOWN, TRUE, 0);
                OverlapDropdownBorder(state->updateSpeedCombo, state->updateSpeedButton);
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeButtonId && HIWORD(wParam) == BN_CLICKED)
            {
                EnableWindow(state->windowSizeButton, FALSE);
                ShowWindow(state->windowSizeCombo, SW_SHOW);
                SetWindowPos(state->windowSizeButton, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetFocus(state->windowSizeCombo);
                SendMessageW(state->windowSizeCombo, CB_SHOWDROPDOWN, TRUE, 0);
                OverlapDropdownBorder(state->windowSizeCombo, state->windowSizeButton);
                return 0;
            }
            if (state && LOWORD(wParam) == kUpdateSpeedControlId && HIWORD(wParam) == CBN_CLOSEUP)
            {
                ShowWindow(state->updateSpeedCombo, SW_HIDE);
                EnableWindow(state->updateSpeedButton, TRUE);
                InvalidateRect(state->updateSpeedButton, nullptr, FALSE);
                SetFocus(window);
                return 0;
            }
            if (state && LOWORD(wParam) == kUpdateSpeedControlId && HIWORD(wParam) == CBN_SELCHANGE)
            {
                const LRESULT selection = SendMessageW(state->updateSpeedCombo, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection < static_cast<LRESULT>(kSamplingOptions.size()))
                {
                    state->sampleIntervalMs = kSamplingOptions[selection].intervalMs;
                    SaveSettings(*state);
                    SetTimer(window, kTimerId, state->sampleIntervalMs, nullptr);
                    if (!state->gpus.empty())
                    {
                        UpdateGpuStatus(*state);
                    }
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeControlId && HIWORD(wParam) == CBN_CLOSEUP)
            {
                ShowWindow(state->windowSizeCombo, SW_HIDE);
                EnableWindow(state->windowSizeButton, TRUE);
                InvalidateRect(state->windowSizeButton, nullptr, FALSE);
                SetFocus(window);
                return 0;
            }
            if (state && LOWORD(wParam) == kWindowSizeControlId && HIWORD(wParam) == CBN_SELCHANGE)
            {
                const LRESULT selection = SendMessageW(state->windowSizeCombo,
                    CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection < static_cast<LRESULT>(kWindowSizeOptions.size()))
                {
                    state->graphWindowMs = kWindowSizeOptions[selection].durationMs;
                    SaveSettings(*state);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_CTLCOLORLISTBOX:
            if (state && state->dropdownBackground)
            {
                const std::array<HWND, 2> combos{state->updateSpeedCombo, state->windowSizeCombo};
                for (HWND combo : combos)
                {
                    COMBOBOXINFO info{sizeof(info)};
                    if (combo && GetComboBoxInfo(combo, &info) && reinterpret_cast<HWND>(lParam) == info.hwndList)
                    {
                        SetTextColor(reinterpret_cast<HDC>(wParam), kDropdownText);
                        SetBkColor(reinterpret_cast<HDC>(wParam), kDropdownBackground);
                        return reinterpret_cast<LRESULT>(state->dropdownBackground);
                    }
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_DRAWITEM:
            if (state && state->dropdownBackground)
            {
                auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                HWND combo = nullptr;
                if (item->CtlType == ODT_BUTTON && item->CtlID == kUpdateSpeedButtonId)
                {
                    combo = state->updateSpeedCombo;
                }
                else if (item->CtlType == ODT_BUTTON && item->CtlID == kWindowSizeButtonId)
                {
                    combo = state->windowSizeCombo;
                }
                if (combo)
                {
                    FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                    if (state->dropdownBorder)
                    {
                        FrameRect(item->hDC, &item->rcItem, state->dropdownBorder);
                    }

                    wchar_t label[32]{};
                    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                    if (selection >= 0)
                    {
                        SendMessageW(combo, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(label));
                    }
                    const HGDIOBJ previousFont = SelectObject(item->hDC, state->fonts.body);
                    SetTextColor(item->hDC, kDropdownText);
                    SetBkMode(item->hDC, TRANSPARENT);
                    RECT textBounds = item->rcItem;
                    textBounds.left += 8;
                    textBounds.right -= 22;
                    DrawTextW(item->hDC, label, -1, &textBounds,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(item->hDC, previousFont);

                    POINT arrow[] = {
                        {item->rcItem.right - 16, item->rcItem.top + 11},
                        {item->rcItem.right - 8, item->rcItem.top + 11},
                        {item->rcItem.right - 12, item->rcItem.top + 16},
                    };
                    HPEN arrowPen = CreatePen(PS_SOLID, 1, kDropdownText);
                    HBRUSH arrowBrush = CreateSolidBrush(kDropdownText);
                    const HGDIOBJ previousPen = SelectObject(item->hDC, arrowPen);
                    const HGDIOBJ previousBrush = SelectObject(item->hDC, arrowBrush);
                    Polygon(item->hDC, arrow, static_cast<int>(std::size(arrow)));
                    SelectObject(item->hDC, previousPen);
                    SelectObject(item->hDC, previousBrush);
                    DeleteObject(arrowPen);
                    DeleteObject(arrowBrush);
                    return TRUE;
                }
                combo = nullptr;
                if (item->CtlType == ODT_COMBOBOX && item->CtlID == kUpdateSpeedControlId)
                {
                    combo = state->updateSpeedCombo;
                }
                else if (item->CtlType == ODT_COMBOBOX && item->CtlID == kWindowSizeControlId)
                {
                    combo = state->windowSizeCombo;
                }
                if (combo)
                {
                    FillRect(item->hDC, &item->rcItem, state->dropdownBackground);
                    if (item->itemID != static_cast<UINT>(-1))
                    {
                        wchar_t label[32]{};
                        SendMessageW(combo, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(label));
                        const HGDIOBJ previousFont = SelectObject(item->hDC, state->fonts.body);
                        SetTextColor(item->hDC, kDropdownText);
                        SetBkMode(item->hDC, TRANSPARENT);
                        RECT textBounds = item->rcItem;
                        textBounds.left += 8;
                        textBounds.right -= 18;
                        DrawTextW(item->hDC, label, -1, &textBounds,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        SelectObject(item->hDC, previousFont);
                    }
                    return TRUE;
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_TIMER:
            if (state && wParam == kTimerId && state->providersReady)
            {
                Refresh(*state);
                if (!state->paused)
                {
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (state && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                if (wParam == VK_OEM_PLUS || wParam == VK_ADD)
                {
                    ApplyZoom(window, *state, state->fontStep + 1);
                    return 0;
                }
                if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)
                {
                    ApplyZoom(window, *state, state->fontStep - 1);
                    return 0;
                }
                if (wParam == '0' || wParam == VK_NUMPAD0)
                {
                    ApplyZoom(window, *state, 0);
                    return 0;
                }
            }
            if (state && wParam == VK_SPACE && (lParam & (1 << 30)) == 0 && !state->gpus.empty())
            {
                state->paused = !state->paused;
                if (state->paused)
                {
                    state->pausedTick = GetTickCount64();
                    state->pausedGpus = state->gpus;
                    state->pausedSystem = state->system;
                }
                else
                {
                    state->pausedGpus = {};
                    state->pausedSystem = {};
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_SIZE:
            if (state)
            {
                PositionHeaderControls(window, *state);
                UpdateScrollBar(window, *state);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            if (state)
            {
                if (!state->mouseInside)
                {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                    TrackMouseEvent(&track);
                    state->mouseInside = true;
                }
                state->mouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && !state->gpus.empty())
            {
                RECT client{};
                GetClientRect(window, &client);
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                for (const RECT& labelRect : state->axisLabelRects)
                {
                    if (PtInRect(&labelRect, point))
                    {
                        state->showClockTime = !state->showClockTime;
                        SaveSettings(*state);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                if (point.y >= client.bottom - kScrollStripHeight)
                {
                    const ScrollGeometry scroll = ComputeScroll(*state, client, DisplayNow(*state));
                    state->dragGrabX = PtInRect(&scroll.thumb, point)
                        ? point.x - static_cast<int>(scroll.thumb.left)
                        : static_cast<int>(scroll.thumb.right - scroll.thumb.left) / 2;
                    state->draggingScroll = true;
                    SetCapture(window);
                    DragScrollThumb(*state, scroll, point.x);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                if (point.x >= client.right - kScrollStripHeight && point.y < client.bottom - kScrollStripHeight)
                {
                    const VerticalScroll vertical = ComputeVerticalScroll(*state, client);
                    state->dragGrabY = PtInRect(&vertical.thumb, point)
                        ? point.y - static_cast<int>(vertical.thumb.top)
                        : static_cast<int>(vertical.thumb.bottom - vertical.thumb.top) / 2;
                    state->draggingVScroll = true;
                    SetCapture(window);
                    DragVerticalThumb(*state, vertical, point.y);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                const Layout layout = ComputeLayout(*state, client.bottom - kScrollStripHeight);
                for (const ProcessHeaderHit& hit : state->processHeaderHits)
                {
                    if (PtInRect(&hit.bounds, point))
                    {
                        if (state->processSortColumn == hit.column)
                        {
                            state->processSortAscending = !state->processSortAscending;
                        }
                        else
                        {
                            state->processSortColumn = hit.column;
                            state->processSortAscending = hit.column == kColumnName || hit.column == kColumnPid;
                        }
                        SaveSettings(*state);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                for (const GridScrollHit& hit : state->gridScrollHits)
                {
                    const RECT grab{hit.track.left - 6, hit.track.top, hit.track.right + 2, hit.track.bottom};
                    if (PtInRect(&grab, point) && hit.panel < state->selections.size())
                    {
                        state->draggingGridPanel = hit.panel;
                        SetCapture(window);
                        ScrollGridTo(*state, hit, point.y);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                for (const ProcessRowHit& hit : state->processRowHits)
                {
                    if (PtInRect(&hit.bounds, point) && hit.panel < state->selections.size())
                    {
                        PanelSelection& selection = state->selections[hit.panel];
                        const bool deselect = selection.active && selection.pid == hit.pid;
                        selection.active = !deselect;
                        selection.pid = deselect ? 0 : hit.pid;
                        if (!deselect)
                        {
                            RecordSelections(*state, GetTickCount64());
                        }
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                const auto headerHit = [&](int top)
                {
                    const int y = top - state->scrollPosition;
                    const RECT header{18, y, client.right - kScrollStripHeight - 8, y + kPanelHeaderHeight};
                    return PtInRect(&header, point) != FALSE;
                };
                const auto nextView = [](int view) { return (view + 1) % kViewCount; };
                bool toggled = true;
                if (headerHit(layout.systemTop))
                {
                    state->systemView = nextView(state->systemView);
                }
                else if (state->gpus.size() > 1 && headerHit(layout.combinedTop))
                {
                    state->combinedView = nextView(state->combinedView);
                }
                else
                {
                    toggled = false;
                    for (size_t index = 0; index < state->gpus.size() && index < kMaxViewGpus; ++index)
                    {
                        if (headerHit(layout.cardTops[index]))
                        {
                            SetGpuView(*state, index, nextView(GpuView(*state, index)));
                            toggled = true;
                            break;
                        }
                    }
                }
                if (toggled)
                {
                    // Without a visible grid there is no selection, so the chart reverts to the global view.
                    for (size_t panel = 0; panel < state->selections.size(); ++panel)
                    {
                        const int view = panel < state->gpus.size() ? GpuView(*state, panel)
                                         : panel == state->gpus.size() ? state->combinedView
                                                                       : state->systemView;
                        if (view == kViewCharts || view == kViewCollapsed)
                        {
                            state->selections[panel].active = false;
                        }
                    }
                    SaveSettings(*state);
                    UpdateScrollBar(window, *state);
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (state)
            {
                state->mouseInside = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (state)
            {
                if (!state->mouseInside)
                {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                    TrackMouseEvent(&track);
                    state->mouseInside = true;
                }
                state->mouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && state->draggingScroll)
            {
                RECT client{};
                GetClientRect(window, &client);
                const ScrollGeometry scroll = ComputeScroll(*state, client, DisplayNow(*state));
                DragScrollThumb(*state, scroll, GET_X_LPARAM(lParam));
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && state->draggingVScroll)
            {
                RECT client{};
                GetClientRect(window, &client);
                DragVerticalThumb(*state, ComputeVerticalScroll(*state, client), GET_Y_LPARAM(lParam));
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && state->draggingGridPanel != static_cast<size_t>(-1))
            {
                for (const GridScrollHit& hit : state->gridScrollHits)
                {
                    if (hit.panel == state->draggingGridPanel)
                    {
                        ScrollGridTo(*state, hit, GET_Y_LPARAM(lParam));
                        InvalidateRect(window, nullptr, FALSE);
                        break;
                    }
                }
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (state && state->draggingGridPanel != static_cast<size_t>(-1))
            {
                state->draggingGridPanel = static_cast<size_t>(-1);
                if (message == WM_LBUTTONUP)
                {
                    ReleaseCapture();
                }
            }
            if (state && state->draggingVScroll)
            {
                state->draggingVScroll = false;
                if (message == WM_LBUTTONUP)
                {
                    ReleaseCapture();
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            if (state && state->draggingScroll)
            {
                state->draggingScroll = false;
                if (message == WM_LBUTTONUP)
                {
                    ReleaseCapture();
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (state)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(window, &point);
                for (const GridScrollHit& hit : state->gridScrollHits)
                {
                    if (PtInRect(&hit.body, point) && hit.panel < state->selections.size())
                    {
                        int& offset = state->selections[hit.panel].gridScroll;
                        offset = std::clamp(offset - delta / WHEEL_DELTA * 3, 0, hit.total - hit.visible);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                SetScrollPosition(window, *state,
                    state->scrollPosition - delta / WHEEL_DELTA * 48);
            }
            return 0;
        case kClipboardReadyMessage:
        {
            HGLOBAL memory = reinterpret_cast<HGLOBAL>(lParam);
            if (!OpenClipboard(window))
            {
                GlobalFree(memory);
                return 0;
            }
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, memory))
            {
                GlobalFree(memory);
            }
            CloseClipboard();
            return 0;
        }
        case kExportFailedMessage:
            MessageBoxW(window, L"The export could not be completed.", L"Export", MB_OK | MB_ICONERROR);
            return 0;
        case WM_SYSCOMMAND:
            if (state)
            {
                // The low four bits of a system command are used by Windows.
                switch (wParam & 0xFFF0)
                {
                    case kExportFileCsvId:
                        ExportToFile(window, *state, false);
                        return 0;
                    case kExportFileTsvId:
                        ExportToFile(window, *state, true);
                        return 0;
                    case kExportClipboardCsvId:
                        ExportToClipboard(window, *state, false);
                        return 0;
                    case kExportClipboardTsvId:
                        ExportToClipboard(window, *state, true);
                        return 0;
                    case kZoomInId:
                        ApplyZoom(window, *state, state->fontStep + 1);
                        return 0;
                    case kZoomOutId:
                        ApplyZoom(window, *state, state->fontStep - 1);
                        return 0;
                    case kZoomResetId:
                        ApplyZoom(window, *state, 0);
                        return 0;
                    case kKeepProcessHistoryId:
                        state->keepProcessHistory = !state->keepProcessHistory;
                        CheckMenuItem(GetSystemMenu(window, FALSE), kKeepProcessHistoryId,
                            MF_BYCOMMAND | (state->keepProcessHistory ? MF_CHECKED : MF_UNCHECKED));
                        RecordSelections(*state, GetTickCount64());
                        SaveSettings(*state);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    default:
                        break;
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = 680;
            limits->ptMinTrackSize.y = 360;
            return 0;
        }
        case WM_PAINT:
            if (state)
                PaintWindow(window, *state);
            else
                return DefWindowProcW(window, message, wParam, lParam);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (state)
            {
                KillTimer(window, kTimerId);
                state->providers.clear();
                DestroyFonts(state->fonts);
                if (state->dropdownBackground)
                    DeleteObject(state->dropdownBackground);
                if (state->dropdownBorder)
                    DeleteObject(state->dropdownBorder);
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    AppState state;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass))
    {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_COMPOSITED, kWindowClass, L"GPU Monitor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        nullptr, nullptr, instance, &state);
    if (!window)
    {
        return 1;
    }

    FitWindowToContent(window, state);
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetFocus(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

