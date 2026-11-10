#pragma once
#include <windows.h>

#include <string>

#include "app_state.h"

void LoadSettings(AppState& state);
void SaveSettings(const AppState& state);
LRESULT FindSamplingOption(const AppState& state);
LRESULT FindWindowSizeOption(const AppState& state);
std::wstring SamplingIntervalLabel(int intervalMs);
std::wstring LoadExportFolder();
void SaveExportFolder(const std::wstring& folder);
