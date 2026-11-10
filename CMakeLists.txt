cmake_minimum_required(VERSION 3.20)
project(GpuMonitor LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "GpuMonitor currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(NVML_INCLUDE_DIR nvml.h
    HINTS "$ENV{CUDA_PATH}"
    PATH_SUFFIXES include
    REQUIRED
)
find_library(NVML_LIBRARY nvml
    HINTS "$ENV{CUDA_PATH}"
    PATH_SUFFIXES lib/x64 lib64
    REQUIRED
)

add_executable(GpuMonitor WIN32
    src/main.cpp
    src/chart.cpp
    src/draw_util.cpp
    src/export.cpp
    src/format.cpp
    src/gpu_provider.cpp
    src/layout.cpp
    src/nvml_provider.cpp
    src/panels.cpp
    src/process_list.cpp
    src/sampling.cpp
    src/scroll.cpp
    src/settings.cpp
    src/window_ui.cpp
    src/resources.rc)
target_include_directories(GpuMonitor PRIVATE "${NVML_INCLUDE_DIR}")
target_link_libraries(GpuMonitor PRIVATE "${NVML_LIBRARY}" user32 gdi32 comdlg32 psapi)
target_compile_definitions(GpuMonitor PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)