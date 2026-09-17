#include "plugin/player_window.hpp"

#include <windows.h>

#include <winver.h>

#include <atomic>
#include <vector>

#include "diagnostics/log.hpp"
#include "plugin/window.hpp"
#include "plugin/window_enum.hpp"

namespace plugin {

namespace {

// WM_USER-based query interface, measured live against the real player
// (docs/FINDINGS.md section 4) — not documented anywhere PotPlayer ships.
constexpr UINT kQueryMessage = 0x0400;
constexpr WPARAM kQueryPositionMs = 0x5004;
constexpr WPARAM kQueryDurationMs = 0x5002;
constexpr WPARAM kQueryStatus = 0x5006;
constexpr wchar_t kMainWindowClass[] = L"PotPlayer64";

// Never a real handle across process lifetimes; just a refcount-free cache
// re-validated with IsWindow() on every use, so a stale value here can never
// cause a query against the wrong (recycled) window.
std::atomic<HWND> g_mainWindow{nullptr};

// Enumerates every top-level window owned by our own process (never anyone
// else's, per PTS-009's "do not hardcode a window handle" design note) and
// hands the result to SelectWindow to pick the live PotPlayer64 window.
HWND FindMainWindow() {
    const auto enumerated = EnumerateOwnProcessWindows();
    const auto selected = SelectWindow(enumerated.candidates, kMainWindowClass);
    if (!selected) {
        return nullptr;
    }
    return enumerated.handlesByValue.at(*selected);
}

// Re-resolves by class whenever the cached handle is unset or has gone
// stale (the window was recreated), per PTS-009's caching requirement.
HWND ResolveMainWindow() {
    HWND cached = g_mainWindow.load(std::memory_order_relaxed);
    if (cached != nullptr && IsWindow(cached)) {
        return cached;
    }

    HWND found = FindMainWindow();
    g_mainWindow.store(found, std::memory_order_relaxed);
    if (found == nullptr) {
        LOG_WARN("plugin: PotPlayer64 main window not found");
    }
    return found;
}

LRESULT Query(WPARAM wParam) {
    const HWND hwnd = ResolveMainWindow();
    if (hwnd == nullptr) {
        return 0;
    }
    return SendMessageW(hwnd, kQueryMessage, wParam, 0);
}

}  // namespace

core::Milliseconds GetPositionMs() {
    return static_cast<core::Milliseconds>(Query(kQueryPositionMs));
}

core::Milliseconds GetDurationMs() {
    return static_cast<core::Milliseconds>(Query(kQueryDurationMs));
}

int GetStatus() {
    return static_cast<int>(Query(kQueryStatus));
}

bool IsFileOpen() {
    return GetDurationMs() > 0;
}

std::uintptr_t GetMainWindowHandle() {
    return reinterpret_cast<std::uintptr_t>(ResolveMainWindow());
}

std::optional<std::string> GetPotPlayerVersion() {
    const HMODULE module = GetModuleHandleW(L"PotPlayer64.dll");
    if (module == nullptr) {
        return std::nullopt;
    }

    wchar_t path[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (pathLength == 0 || pathLength >= std::size(path)) {
        return std::nullopt;
    }

    DWORD unusedHandle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(path, &unusedHandle);
    if (infoSize == 0) {
        return std::nullopt;
    }
    std::vector<BYTE> buffer(infoSize);
    if (!GetFileVersionInfoW(path, 0, infoSize, buffer.data())) {
        return std::nullopt;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoLen = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoLen) ||
        fixedInfo == nullptr) {
        return std::nullopt;
    }

    return std::to_string(HIWORD(fixedInfo->dwFileVersionMS)) + "." +
           std::to_string(LOWORD(fixedInfo->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(fixedInfo->dwFileVersionLS)) + "." +
           std::to_string(LOWORD(fixedInfo->dwFileVersionLS));
}

}  // namespace plugin
