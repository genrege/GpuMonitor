# NVIDIA GPU Monitor

A small Windows desktop monitor that reads live utilization and temperature for every NVIDIA GPU through the NVML API. It samples every two seconds and keeps 31 readings per GPU, covering the current instant through 60 seconds ago.

## Requirements

- Windows 10 or 11
- Visual Studio 2026 C++ build tools (v145) for the solution, or Visual Studio 2022 C++ build tools for the CMake build
- NVIDIA driver with `nvml.dll` available at runtime
- NVIDIA CUDA Toolkit with `nvml.h` and `nvml.lib`
- `CUDA_PATH` set to the CUDA Toolkit directory (for example, `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9`)

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run `build\Release\GpuMonitor.exe`. The graph starts filling after launch; unavailable NVML readings are shown as `--`.

## Visual Studio 2026

Open `GpuMonitor.sln` and build the `x64` platform in Debug or Release, or run this from a Visual Studio 2026 Developer PowerShell:

```powershell
msbuild GpuMonitor.sln /p:Configuration=Release /p:Platform=x64
```

The executable is written to `x64\Release\GpuMonitor.exe`. The project uses `CUDA_PATH` to locate `nvml.h` and `nvml.lib`.