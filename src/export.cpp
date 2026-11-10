#include "export.h"

#include <commdlg.h>

#include <algorithm>
#include <cwchar>
#include <thread>

#include "settings.h"

namespace
{

// A copy of the recorded data, so exporting on a worker thread never blocks sampling.
struct ExportSnapshot
{
    std::vector<GpuInfo> gpus;
    SystemInfo system;
};

// One row per sample for every recorded series.
std::wstring BuildExport(const ExportSnapshot& state, wchar_t separator)
{
    int rows = state.system.historyCount;
    for (const GpuInfo& gpu : state.gpus)
    {
        rows = std::min(rows, gpu.historyCount);
    }

    std::wstring text;
    const auto field = [&](const std::wstring& value)
    {
        if (value.find_first_of(std::wstring(L"\"\r\n") + separator) == std::wstring::npos)
        {
            return value;
        }
        std::wstring quoted = L"\"";
        for (wchar_t character : value)
        {
            quoted += character;
            if (character == L'"')
            {
                quoted += L'"';
            }
        }
        return quoted + L"\"";
    };
    const auto appendRow = [&](const std::vector<std::wstring>& values)
    {
        for (size_t index = 0; index < values.size(); ++index)
        {
            text += (index ? std::wstring(1, separator) : std::wstring()) + field(values[index]);
        }
        text += L"\r\n";
    };

    std::vector<std::wstring> header{L"time"};
    for (size_t index = 0; index < state.gpus.size(); ++index)
    {
        const std::wstring prefix = L"GPU" + std::to_wstring(index) + L" ";
        header.push_back(prefix + L"load %");
        header.push_back(prefix + L"vram used MB");
        header.push_back(prefix + L"vram total MB");
    }
    header.insert(header.end(), {L"CPU load %", L"RAM used MB", L"RAM total MB"});
    appendRow(header);

    FILETIME utcNow{};
    GetSystemTimeAsFileTime(&utcNow);
    ULARGE_INTEGER base{};
    base.LowPart = utcNow.dwLowDateTime;
    base.HighPart = utcNow.dwHighDateTime;
    const ULONGLONG nowTick = GetTickCount64();
    const auto timestamp = [&](ULONGLONG tick)
    {
        ULARGE_INTEGER past{};
        past.QuadPart = base.QuadPart - (nowTick > tick ? nowTick - tick : 0) * 10000ULL;
        const FILETIME utc{past.LowPart, past.HighPart};
        FILETIME local{};
        SYSTEMTIME time{};
        FileTimeToLocalFileTime(&utc, &local);
        FileTimeToSystemTime(&local, &time);
        wchar_t buffer[40]{};
        swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d.%03d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        return std::wstring(buffer);
    };
    const auto ringIndex = [&](int next, int row)
    {
        return (next - rows + row + kHistoryCapacity) % kHistoryCapacity;
    };

    for (int row = 0; row < rows; ++row)
    {
        const SystemSample& system = state.system.history[ringIndex(state.system.historyNext, row)];
        std::vector<std::wstring> values{timestamp(system.timestampMs)};
        for (const GpuInfo& gpu : state.gpus)
        {
            const Sample& sample = gpu.history[ringIndex(gpu.historyNext, row)];
            values.push_back(sample.valid ? std::to_wstring(sample.load) : L"");
            values.push_back(sample.memoryValid ? std::to_wstring(sample.memoryUsed / 1048576) : L"");
            values.push_back(sample.memoryValid ? std::to_wstring(sample.memoryTotal / 1048576) : L"");
        }
        values.push_back(system.cpuValid ? std::to_wstring(system.cpuPercent) : L"");
        values.push_back(system.ramValid ? std::to_wstring(system.ramUsed / 1048576) : L"");
        values.push_back(system.ramValid ? std::to_wstring(system.ramTotal / 1048576) : L"");
        appendRow(values);
    }
    return text;
}

} // namespace

void ExportToFile(HWND window, const AppState& state, bool tsv)
{
    if (state.system.historyCount == 0)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    wchar_t path[MAX_PATH]{};
    wcscpy_s(path, tsv ? L"data.tsv" : L"data.csv");
    const std::wstring folder = LoadExportFolder();
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window;
    dialog.lpstrFilter = tsv ? L"TSV files (*.tsv)\0*.tsv\0All files (*.*)\0*.*\0"
                             : L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrDefExt = tsv ? L"tsv" : L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog))
    {
        return;
    }

    const std::wstring savedPath = path;
    const size_t slash = savedPath.find_last_of(L'\\');
    if (slash != std::wstring::npos)
    {
        SaveExportFolder(savedPath.substr(0, slash));
    }

    std::thread(
        [window, snapshot = ExportSnapshot{state.gpus, state.system}, savedPath, tsv]()
        {
            const std::wstring text = BuildExport(snapshot, tsv ? L'\t' : L',');
            std::string utf8 = "\xEF\xBB\xBF";
            const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            const size_t bomSize = utf8.size();
            utf8.resize(bomSize + static_cast<size_t>(length));
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data() + bomSize, length, nullptr, nullptr);

            HANDLE file = CreateFileW(savedPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD written = 0;
            const bool saved = file != INVALID_HANDLE_VALUE
                && WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)
                && written == utf8.size();
            if (file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(file);
            }
            if (!saved)
            {
                PostMessageW(window, kExportFailedMessage, 0, 0);
            }
        })
        .detach();
}

void ExportToClipboard(HWND window, const AppState& state, bool tsv)
{
    if (state.system.historyCount == 0)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::thread(
        [window, snapshot = ExportSnapshot{state.gpus, state.system}, tsv]()
        {
            const std::wstring text = BuildExport(snapshot, tsv ? L'\t' : L',');
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            void* destination = memory ? GlobalLock(memory) : nullptr;
            if (!destination)
            {
                if (memory)
                {
                    GlobalFree(memory);
                }
                PostMessageW(window, kExportFailedMessage, 0, 0);
                return;
            }
            memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            // The window thread owns the clipboard call; it takes over the memory block.
            PostMessageW(window, kClipboardReadyMessage, 0, reinterpret_cast<LPARAM>(memory));
        })
        .detach();
}

void AddExportMenus(HWND window)
{
    HMENU systemMenu = GetSystemMenu(window, FALSE);
    HMENU fileMenu = CreatePopupMenu();
    HMENU clipboardMenu = CreatePopupMenu();
    if (!systemMenu || !fileMenu || !clipboardMenu)
    {
        return;
    }
    AppendMenuW(fileMenu, MF_STRING, kExportFileCsvId, L"CSV (comma separated)...");
    AppendMenuW(fileMenu, MF_STRING, kExportFileTsvId, L"TSV (tab separated)...");
    AppendMenuW(clipboardMenu, MF_STRING, kExportClipboardCsvId, L"CSV (comma separated)");
    AppendMenuW(clipboardMenu, MF_STRING, kExportClipboardTsvId, L"TSV (tab separated)");

    // Insert before the separator that precedes Close.
    UINT position = static_cast<UINT>(std::max(0, GetMenuItemCount(systemMenu) - 2));
    InsertMenuW(systemMenu, position, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(systemMenu, position + 1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"Export to file");
    InsertMenuW(systemMenu, position + 2, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(clipboardMenu), L"Export to clipboard");
}

