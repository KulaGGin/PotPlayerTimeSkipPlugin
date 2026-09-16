#include "diagnostics/log.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace diagnostics {

namespace {

// Truncated (not rotated) once exceeded — this is a personal tool, a simple
// cap is enough to stop unbounded growth without a rotation policy.
constexpr std::int64_t kMaxLogBytes = 5 * 1024 * 1024;

// Zero-initialized, kernel32/ntdll-only primitives: no runtime constructor
// runs before first use, so touching these from DllMain never itself
// contends the loader lock (unlike e.g. a lazily-constructed std::mutex).
SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_triedOpen = false;

std::atomic<int> g_minSeverity{static_cast<int>(DIAGNOSTICS_DEFAULT_MIN_SEVERITY)};

std::wstring LogFilePath() {
    wchar_t localAppData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::wstring dir = localAppData;
    dir += L"\\PotPlayerTimeSkip";
    CreateDirectoryW(dir.c_str(), nullptr); // ignore ERROR_ALREADY_EXISTS and any failure

    return dir + L"\\plugin.log";
}

// Opens (creating the folder/file as needed) exactly once per process,
// lazily on the first log call rather than eagerly at load time. Must only
// be called with g_lock held.
void EnsureFileOpenLocked() {
    if (g_triedOpen) {
        return;
    }
    g_triedOpen = true;

    const std::wstring path = LogFilePath();
    if (path.empty()) {
        return;
    }

    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(g_file, &size) && size.QuadPart > kMaxLogBytes) {
        CloseHandle(g_file);
        g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
}

void WriteToFileLocked(std::string_view line) {
    EnsureFileOpenLocked();
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(g_file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

// DebugView (and the debugger) read whichever of OutputDebugStringA/W is
// called; going through W with an explicit UTF-8 -> UTF-16 conversion keeps
// non-ASCII content intact regardless of the process's ANSI codepage.
void WriteToDebuggerUtf8(std::string_view utf8Line) {
    if (utf8Line.empty()) {
        return;
    }
    const int wideLength =
        MultiByteToWideChar(CP_UTF8, 0, utf8Line.data(), static_cast<int>(utf8Line.size()),
                             nullptr, 0);
    if (wideLength <= 0) {
        return;
    }

    std::vector<wchar_t> wide(static_cast<std::size_t>(wideLength) + 1);
    MultiByteToWideChar(CP_UTF8, 0, utf8Line.data(), static_cast<int>(utf8Line.size()),
                         wide.data(), wideLength);
    wide[static_cast<std::size_t>(wideLength)] = L'\0';
    OutputDebugStringW(wide.data());
}

Timestamp Now() {
    SYSTEMTIME local;
    GetLocalTime(&local);
    return Timestamp{local.wYear, local.wMonth, local.wDay,
                      local.wHour, local.wMinute, local.wSecond, local.wMilliseconds};
}

} // namespace

void SetMinSeverity(Severity severity) {
    g_minSeverity.store(static_cast<int>(severity), std::memory_order_relaxed);
}

Severity GetMinSeverity() {
    return static_cast<Severity>(g_minSeverity.load(std::memory_order_relaxed));
}

bool IsEnabled(Severity severity) {
    return static_cast<int>(severity) >= g_minSeverity.load(std::memory_order_relaxed);
}

void LogRaw(Severity severity, std::string_view utf8Message) {
    const std::string line = FormatLine(severity, Now(), GetCurrentThreadId(), utf8Message);

    AcquireSRWLockExclusive(&g_lock);
    WriteToFileLocked(line);
    ReleaseSRWLockExclusive(&g_lock);

    WriteToDebuggerUtf8(line);
}

}
