# Agent notes

Win32 C++17 GPU/system monitor (NVML for GPUs). See README.md for user-facing behaviour.

## Build

- Visual Studio: `msbuild GpuMonitor.sln /p:Configuration=Debug /p:Platform=x64 /m /v:m` (output in `x64\<Config>\`). A running `GpuMonitor.exe` locks the output and fails the link; close it or build Debug.
- CMake: `cmake -S . -B build; cmake --build build --config Release`.
- Needs `CUDA_PATH` for `nvml.h`/`nvml.lib`.
- New source files must be added to `CMakeLists.txt`, `GpuMonitor.vcxproj` and `GpuMonitor.vcxproj.filters`.
- There are no automated tests; verify by building and running.

## Layout (src/)

- `main.cpp`: window procedure, input (clicks, wheel, keys, menus).
- `window_ui.cpp`: painting, header controls, scrolling.
- `layout.cpp`: panel positions and heights; per-panel view modes (`PanelView`: charts, charts+grid, grid, collapsed).
- `panels.cpp`: panel drawing and the process grid.
- `chart.cpp`: time-series charts, including the optional red selection overlay.
- `sampling.cpp`: periodic refresh, process refresh (1 s), selection/history recording.
- `process_list.cpp`: system process enumeration (CPU %, private working set), merge and sort.
- `gpu_provider.h` / `nvml_provider.cpp`: vendor-neutral provider interface and the NVML implementation, including per-process GPU data.
- `settings.cpp`: registry persistence under `HKCU\Software\NvidiaInfoGpuMonitor`.
- `export.cpp`: CSV/TSV export (does not include process lists).
- `draw_util.cpp`: fonts and drawing helpers; chart and grid fonts scale with the zoom step.
- `config.h`: constants, colours, menu ids.

## Conventions

- Hit-test rectangles (header, row, grid scrollbar, chart labels) are collected during paint into `AppState` and used by mouse handlers; keep them in the same coordinates as painting.
- Painting uses a frozen snapshot while paused (`pausedGpus`, `pausedSystem`); selections and their histories are live.
- Commands added to the system menu must use ids with the low four bits clear (`WM_SYSCOMMAND` masks them).
- New persisted settings go in `LoadSettings`/`SaveSettings` as DWORD values.
- Keep comments short and only for what the code cannot show.
