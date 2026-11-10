#pragma once
#include <windows.h>

#include "app_state.h"

void ExportToFile(HWND window, const AppState& state, bool tsv);
void ExportToClipboard(HWND window, const AppState& state, bool tsv);
void AddExportMenus(HWND window);
