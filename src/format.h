#pragma once
#include <windows.h>

#include <string>

std::wstring ToWide(const char* text);
std::wstring FormatAgo(ULONGLONG ms);
std::wstring FormatClock(ULONGLONG nowTick, ULONGLONG tick);
std::wstring FormatMegabytes(unsigned long long bytes);
std::wstring FormatMemory(unsigned long long bytes);
