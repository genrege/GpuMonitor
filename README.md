# NVIDIA GPU Monitor

A Windows desktop monitor for NVIDIA GPUs. It displays GPU utilization, temperature, and VRAM usage, with history graphs for utilization and memory.

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
- Both selections are saved automatically for the current Windows user and restored next time. If no saved setting is available, the defaults are a 1-second update and a 1-minute graph window.
- When more than one NVIDIA GPU is detected, a combined VRAM row is shown below the GPU cards.

## GPU Support

GPU discovery and telemetry use NVIDIA's NVML API; this application does not use a vendor-neutral GPU interface. AMD and Intel GPUs are not detected or monitored. On a system with GPUs from multiple vendors, only NVIDIA GPUs exposed through NVML are listed. If NVML initializes successfully but finds no supported NVIDIA GPU, the application reports `No NVIDIA GPUs found`.

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