#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct ProcessRow
{
    unsigned long pid = 0;
    std::wstring name;
    double load = 0;
    bool loadValid = false;
    unsigned long long memory = 0;
    bool memoryValid = false;
};

enum ProcessColumn
{
    kColumnName = 0,
    kColumnPid = 1,
    kColumnLoad = 2,
    kColumnMemory = 3,
    kColumnCount = 4,
};

// Cumulative CPU times between refreshes, keyed by pid, to derive per-process CPU load.
struct ProcessCpuTracker
{
    struct Entry
    {
        unsigned long pid;
        ULONGLONG cpuTime;
    };
    std::vector<Entry> previous;
    ULONGLONG previousTickMs = 0;
};

std::wstring ProcessNameFromPid(unsigned long pid);
std::vector<ProcessRow> QuerySystemProcesses(ProcessCpuTracker& tracker, ULONGLONG nowMs);
// Sums load and memory per pid across several per-GPU lists.
std::vector<ProcessRow> MergeProcessRows(const std::vector<const std::vector<ProcessRow>*>& lists);
void SortProcessRows(std::vector<ProcessRow>& rows, int column, bool ascending);
