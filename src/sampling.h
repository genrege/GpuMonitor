#pragma once
#include "app_state.h"

void InitializeProviders(AppState& state);
void UpdateGpuStatus(AppState& state);
void Refresh(AppState& state);
// Appends the current reading of each selected process to its panel's selection history.
void RecordSelections(AppState& state, ULONGLONG timestampMs);
