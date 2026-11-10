#include "process_list.h"

#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>

namespace
{
struct HandleCloser
{
    HANDLE handle;
    ~HandleCloser()
    {
        if (handle)
            CloseHandle(handle);
    }
};

ULONGLONG ToValue(const FILETIME& time)
{
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
} // namespace

std::wstring ProcessNameFromPid(unsigned long pid)
{
    HandleCloser process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (process.handle)
    {
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process.handle, 0, path, &size))
        {
            const wchar_t* name = wcsrchr(path, L'\\');
            return name ? name + 1 : path;
        }
    }
    return L"PID " + std::to_wstring(pid);
}

std::vector<ProcessRow> QuerySystemProcesses(ProcessCpuTracker& tracker, ULONGLONG nowMs)
{
    std::vector<ProcessRow> rows;
    HandleCloser snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.handle == INVALID_HANDLE_VALUE)
    {
        snapshot.handle = nullptr;
        return rows;
    }

    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const double elapsed100ns = static_cast<double>(nowMs - tracker.previousTickMs) * 10000.0 *
                                std::max<DWORD>(1, system.dwNumberOfProcessors);
    const bool hasPrevious = tracker.previousTickMs != 0 && elapsed100ns > 0;

    std::vector<ProcessCpuTracker::Entry> current;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot.handle, &entry); more; more = Process32NextW(snapshot.handle, &entry))
    {
        if (entry.th32ProcessID == 0)
        {
            continue;
        }
        ProcessRow row;
        row.pid = entry.th32ProcessID;
        row.name = entry.szExeFile;

        HandleCloser process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid)};
        if (process.handle)
        {
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process.handle, &creation, &exit, &kernel, &user))
            {
                const ULONGLONG cpuTime = ToValue(kernel) + ToValue(user);
                current.push_back({row.pid, cpuTime});
                const auto previous = std::find_if(tracker.previous.begin(), tracker.previous.end(),
                    [&](const ProcessCpuTracker::Entry& item) { return item.pid == row.pid; });
                if (hasPrevious && previous != tracker.previous.end() && cpuTime >= previous->cpuTime)
                {
                    row.load = std::min(100.0, static_cast<double>(cpuTime - previous->cpuTime) * 100.0 / elapsed100ns);
                    row.loadValid = true;
                }
            }
            PROCESS_MEMORY_COUNTERS_EX2 counters{};
            if (GetProcessMemoryInfo(process.handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
            {
                row.memory = counters.PrivateWorkingSetSize;
                row.memoryValid = true;
            }
        }
        rows.push_back(std::move(row));
    }
    tracker.previous = std::move(current);
    tracker.previousTickMs = nowMs;
    return rows;
}

std::vector<ProcessRow> MergeProcessRows(const std::vector<const std::vector<ProcessRow>*>& lists)
{
    std::vector<ProcessRow> merged;
    for (const std::vector<ProcessRow>* list : lists)
    {
        for (const ProcessRow& row : *list)
        {
            auto found = std::find_if(merged.begin(), merged.end(),
                [&](const ProcessRow& item) { return item.pid == row.pid; });
            if (found == merged.end())
            {
                merged.push_back(row);
                continue;
            }
            if (row.loadValid)
            {
                found->load += row.load;
                found->loadValid = true;
            }
            if (row.memoryValid)
            {
                found->memory += row.memory;
                found->memoryValid = true;
            }
        }
    }
    return merged;
}

void SortProcessRows(std::vector<ProcessRow>& rows, int column, bool ascending)
{
    const auto compare = [column](const ProcessRow& a, const ProcessRow& b)
    {
        switch (column)
        {
            case kColumnName:
            {
                const int result = _wcsicmp(a.name.c_str(), b.name.c_str());
                return result != 0 ? result : (a.pid < b.pid ? -1 : (a.pid > b.pid ? 1 : 0));
            }
            case kColumnPid:
                return a.pid < b.pid ? -1 : (a.pid > b.pid ? 1 : 0);
            case kColumnLoad:
            {
                const double x = a.loadValid ? a.load : -1.0;
                const double y = b.loadValid ? b.load : -1.0;
                return x < y ? -1 : (x > y ? 1 : 0);
            }
            default:
            {
                const long long x = a.memoryValid ? static_cast<long long>(a.memory) : -1;
                const long long y = b.memoryValid ? static_cast<long long>(b.memory) : -1;
                return x < y ? -1 : (x > y ? 1 : 0);
            }
        }
    };
    std::stable_sort(rows.begin(), rows.end(),
        [&](const ProcessRow& a, const ProcessRow& b)
        {
            const int result = compare(a, b);
            return ascending ? result < 0 : result > 0;
        });
}
