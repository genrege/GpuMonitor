#pragma once
#include <windows.h>

#include "app_state.h"

void PositionHeaderControls(HWND window, const AppState& state);
void OverlapDropdownBorder(HWND combo, HWND button);
void EnableDarkTitleBar(HWND window);
void FitWindowToContent(HWND window, const AppState& state);
void UpdateScrollBar(HWND window, AppState& state);
void SetScrollPosition(HWND window, AppState& state, int position);
void PaintWindow(HWND window, AppState& state);
