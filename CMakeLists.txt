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

add_executable(GpuMonitor WIN32 src/main.cpp src/resources.rc)
target_include_directories(GpuMonitor PRIVATE "${NVML_INCLUDE_DIR}")
target_link_libraries(GpuMonitor PRIVATE "${NVML_LIBRARY}" user32 gdi32)
target_compile_definitions(GpuMonitor PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)