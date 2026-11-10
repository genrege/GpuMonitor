#include "format.h"

#include <cwchar>

std::wstring ToWide(const char* text)
{
    if (text == nullptr || *text == '\0')
    {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1)
    {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

std::wstring FormatAgo(ULONGLONG ms)
{
    const unsigned int total = static_cast<unsigned int>(ms / 1000);
    if (total < 1)
    {
        return L"now";
    }

    const unsigned int hours = total / 3600;
    const unsigned int minutes = total % 3600 / 60;
    const unsigned int seconds = total % 60;
    std::wstring text;
    const auto append = [&](unsigned int value, const wchar_t* unit)
    {
        if (value > 0)
        {
            text += (text.empty() ? L"" : L" ") + std::to_wstring(value) + L" " + unit;
        }
    };
    append(hours, L"h");
    append(minutes, L"min");
    append(seconds, L"s");
    return text + L" ago";
}

std::wstring FormatMegabytes(unsigned long long bytes)
{
    return std::to_wstring(bytes / 1048576) + L" MB";
}

std::wstring FormatMemory(unsigned long long bytes)
{
    const unsigned long long tenthsOfGigabytes =
        (bytes * 10 + (1ULL << 29)) / (1ULL << 30);
    return std::to_wstring(tenthsOfGigabytes / 10) + L"." + std::to_wstring(tenthsOfGigabytes % 10) + L" GB";
}

// Wall-clock time of a GetTickCount64 timestamp, dated only when it is not today.
std::wstring FormatClock(ULONGLONG nowTick, ULONGLONG tick)
{
    FILETIME utcNow{};
    GetSystemTimeAsFileTime(&utcNow);
    ULARGE_INTEGER value{};
    value.LowPart = utcNow.dwLowDateTime;
    value.HighPart = utcNow.dwHighDateTime;
    ULARGE_INTEGER past{};
    past.QuadPart = value.QuadPart - (nowTick - tick) * 10000ULL;

    const FILETIME utcThen{past.LowPart, past.HighPart};
    FILETIME localThen{};
    FILETIME localNow{};
    FileTimeToLocalFileTime(&utcThen, &localThen);
    FileTimeToLocalFileTime(&utcNow, &localNow);
    SYSTEMTIME then{};
    SYSTEMTIME today{};
    FileTimeToSystemTime(&localThen, &then);
    FileTimeToSystemTime(&localNow, &today);

    wchar_t text[32]{};
    if (then.wDay != today.wDay || then.wMonth != today.wMonth)
    {
        swprintf_s(text, L"%02d-%02d %02d:%02d:%02d", then.wMonth, then.wDay, then.wHour, then.wMinute, then.wSecond);
    }
    else
    {
        swprintf_s(text, L"%02d:%02d:%02d", then.wHour, then.wMinute, then.wSecond);
    }
    return text;
}

