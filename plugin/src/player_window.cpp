#include "plugin/player_window.hpp"

#include <windows.h>

#include <atomic>
#include <unordered_map>

#include "diagnostics/log.hpp"
#include "plugin/window.hpp"

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

struct EnumState {
    DWORD processId;
    std::vector<WindowCandidate> candidates;
    // SelectWindow only ever sees the narrowed integer form of a handle
    // (plugin/window.hpp stays <windows.h>-free); this recovers the real
    // HWND for whichever candidate it picks.
    std::unordered_map<std::uintptr_t, HWND> handlesByValue;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto& state = *reinterpret_cast<EnumState*>(lParam);

    DWORD ownerProcessId = 0;
    GetWindowThreadProcessId(hwnd, &ownerProcessId);
    if (ownerProcessId != state.processId) {
        return TRUE;
    }

    wchar_t className[256];
    const int length = GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (length <= 0) {
        return TRUE;
    }

    const auto handleValue = reinterpret_cast<std::uintptr_t>(hwnd);
    state.candidates.push_back(WindowCandidate{
        handleValue, std::wstring(className, static_cast<std::size_t>(length)), IsWindowVisible(hwnd) != FALSE});
    state.handlesByValue.emplace(handleValue, hwnd);

    return TRUE;
}

// Enumerates every top-level window owned by our own process (never anyone
// else's, per PTS-009's "do not hardcode a window handle" design note) and
// hands the result to SelectWindow to pick the live PotPlayer64 window.
HWND FindMainWindow() {
    EnumState state{GetCurrentProcessId(), {}, {}};
    if (!EnumWindows(&EnumWindowsCallback, reinterpret_cast<LPARAM>(&state))) {
        LOG_WARN("plugin: EnumWindows failed while looking for the main window, gle={}", GetLastError());
        return nullptr;
    }

    const auto selected = SelectWindow(state.candidates, kMainWindowClass);
    if (!selected) {
        return nullptr;
    }
    return state.handlesByValue.at(*selected);
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

}  // namespace plugin
