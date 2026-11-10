# NVIDIA GPU Monitor

A Windows desktop monitor for NVIDIA GPUs and the host system. Each GPU card shows utilization, temperature and VRAM usage, with history charts for load (line) and VRAM (bars). A System panel below the GPUs shows CPU load (line) and system RAM (bars) in the same layout.

## Requirements

- Windows 10 or 11
- Visual Studio 2026 C++ build tools (v145) for the solution, or Visual Studio 2022 C++ build tools for the CMake build
- An NVIDIA GPU supported by the installed NVIDIA Windows driver and NVML
- NVIDIA driver with `nvml.dll` available at runtime
- NVIDIA CUDA Toolkit with `nvml.h` and `nvml.lib` to build the application
- `CUDA_PATH` set to the CUDA Toolkit directory (for example, `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9`)

The CUDA Toolkit is needed to compile the application; running the built application requires the NVIDIA driver and its NVML runtime.

## Use

Launch `GpuMonitor.exe`. The application discovers NVIDIA GPUs exposed by NVML and displays a card for each one. Readings are sampled continuously, and graph history begins when the application starts.

- Use **UPDATE** to choose a sampling interval from 0.1 seconds to 1 hour.
- Use **WINDOW** to choose a graph window from 1 minute to 24 hours.
- Both selections are saved automatically for the current Windows user and restored next time, along with the panel views, process sort, history option, zoom level and the time-label format. If no saved setting is available, the defaults are a 1-second update and a 1-minute graph window.
- When more than one NVIDIA GPU is detected, a combined panel is shown below the GPU cards with the average GPU load (line) and total VRAM used against the sum of every card's VRAM (bars). With a single GPU it is not shown.

### Panels and charts

- Click the header of any GPU card, the combined panel, or the System panel to cycle its view: charts only, charts plus process list, process list only, collapsed, then back to charts only. Each panel keeps its own view, and the views are saved with the other settings.
- All charts share one time axis. The scrollbar along the bottom of the window moves the visible range through recorded history; drag it fully right to follow live data again. Up to 172,801 samples per series are kept in memory (about 48 hours at 1 s) and are lost when the application closes.
- Click either time label under a chart to switch every label between relative form (`2 min 14 s ago`) and local clock time. The choice is saved with the other settings.
- Hover over a chart to see a vertical line at the pointer in every chart, each with a box showing the value at that moment (% or MB) in the chart's colour. The load charts also show the local time in yellow at the top.
- Press **Space** to pause or resume the display. Data keeps being collected while paused, and the charts and readouts resume at the current time.

### Process lists

Every GPU card, the combined panel and the System panel can show a process grid with name, PID, load % and memory. The grid sits to the right of the charts when both are shown.

- GPU lists come from NVML. Load is per-process GPU utilization; memory is VRAM in use. Under WDDM the driver often does not report per-process VRAM, in which case memory shows `--`.
- The combined panel merges processes by PID across GPUs, summing load and memory.
- The System panel lists all processes with CPU % (share of total CPU) and private working set.
- Lists refresh once per second, independent of the update interval, and freeze while paused.
- The grid shows 15 rows at a time; scroll with the mouse wheel over the grid or drag its scrollbar.
- Click a column header to sort by it; click again to reverse. The default is by name. The sort is shared by all grids and saved.
- Hover a row to highlight it. Click a row to overlay that process's history in red on the load and memory charts of the same panel (load as a %, memory against the same total as the main chart). Click it again to clear. The overlay is available only while the panel shows both charts and grid.
- History for a process is recorded from when it is selected. Turn on **Keep history for all processes** in the window's system menu (right-click the title bar) to record every listed process, so the overlay has history immediately. History of a process is discarded when it exits. At most 86,400 samples (24 hours at 1 s) are kept per process.
- Process lists are not included in exports.

### Zoom

Press **Ctrl++** / **Ctrl+-** to enlarge or shrink the chart and process list text, and **Ctrl+0** to reset. The same actions are under **View** in the system menu. The zoom level is saved.

## GPU Support

GPU discovery and telemetry use NVIDIA's NVML API; this application does not use a vendor-neutral GPU interface. AMD and Intel GPUs are not detected or monitored. On a system with GPUs from multiple vendors, only NVIDIA GPUs exposed through NVML are listed. If NVML initializes successfully but finds no supported NVIDIA GPU, the application reports `No NVIDIA GPUs found`.

Official driver downloads:

- [NVIDIA drivers](https://www.nvidia.com/Download/index.aspx) (required for GPU monitoring in this app)
- [AMD drivers](https://www.amd.com/en/support/download/drivers.html) (not monitored by this app)
- [Intel drivers and software](https://www.intel.com/content/www/us/en/download-center/home.html) (not monitored by this app)

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run `build\Release\GpuMonitor.exe`.

## Visual Studio 2026

Open `GpuMonitor.sln` and build the `x64` platform in Debug or Release, or run this from a Visual Studio 2026 Developer PowerShell:

```powershell
msbuild GpuMonitor.sln /p:Configuration=Release /p:Platform=x64
```

The executable is written to `x64\Release\GpuMonitor.exe`. The project uses `CUDA_PATH` to locate `nvml.h` and `nvml.lib`.