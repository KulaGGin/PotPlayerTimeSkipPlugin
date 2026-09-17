#include <windows.h>

#include "diagnostics/log.hpp"
#include "plugin/hotkey_pump.hpp"

// PTS-006 production proxy. All three real exports are forwarded to
// MediaDB64_orig.dll by both name and ordinal (PE forwarder RVAs, resolved
// by the loader — none of this file's own code runs on the call path).
// Ordinals are pinned to match the real DLL's measured layout (see
// docs/FINDINGS.md §5) rather than left to whatever order the linker would
// otherwise assign, in case anything resolves these by ordinal instead of
// name. The real MediaDB64.dll must be renamed to MediaDB64_orig.dll and
// live next to this proxy for these to resolve.
#pragma comment(linker, "/EXPORT:CreateDatabaseEngine=MediaDB64_orig.CreateDatabaseEngine,@1")
#pragma comment(linker, "/EXPORT:CreateJpegDecoder=MediaDB64_orig.CreateJpegDecoder,@2")
#pragma comment(linker, "/EXPORT:CreateSMTC=MediaDB64_orig.CreateSMTC,@3")

namespace {

// Assumes the proxy is attached/detached at most once per process, which
// holds for how PotPlayer actually loads it — not safe against a
// hypothetical rapid reload cycle.
HANDLE g_stopEvent = nullptr;
HMODULE g_selfModule = nullptr;

DWORD WINAPI WorkerThreadProc(LPVOID) {
    LOG_INFO("proxy worker thread started");

    // PTS-013: hotkey registration and dispatch now runs on this thread —
    // RunHotkeyPump blocks here, pumping WM_HOTKEY, until g_stopEvent is
    // signaled below from DLL_PROCESS_DETACH.
    plugin::RunHotkeyPump(g_stopEvent);

    LOG_INFO("proxy worker thread stopping");
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;

    // Releases the extra module reference taken in DLL_PROCESS_ATTACH and
    // exits this thread as one atomic operation — the module is only
    // unmapped after this thread has already left it, so no code here ever
    // runs post-unload. This is also why DLL_PROCESS_DETACH below never
    // WaitForSingleObject's on this thread: this thread's own startup needed
    // the loader lock that such a wait would be holding, which is exactly
    // the deadlock this design avoids (see PTS-006's "loader-lock discipline
    // is the whole game here" note — hit and confirmed via the
    // proxy_dllmain_no_deadlock CTest case before landing on this design).
    FreeLibraryAndExitThread(g_selfModule, 0);
}

}  // namespace

// Logging directly from DllMain here is deliberate: it is the earliest and
// riskiest point in the plugin's lifetime (see PTS-004 / diagnostics/), so
// it doubles as a live smoke test that the logging path is safe to call
// from exactly this context.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        // We don't track per-thread state, so skip the DLL_THREAD_ATTACH/
        // DETACH notification traffic entirely.
        DisableThreadLibraryCalls(module);
        LOG_INFO("MediaDB64 proxy attached");

        // Take our own extra reference to this module (loader-safe: just a
        // refcount bump, no DllMain re-entry, unlike LoadLibrary) so it
        // can't be unmapped until the worker thread itself releases it.
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                 reinterpret_cast<LPCWSTR>(module), &g_selfModule)) {
            LOG_ERROR("proxy: GetModuleHandleEx failed, gle={}", GetLastError());
            break;
        }

        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stopEvent) {
            LOG_ERROR("proxy: CreateEvent failed, gle={}", GetLastError());
            FreeLibrary(g_selfModule);
            g_selfModule = nullptr;
            break;
        }

        const HANDLE thread = CreateThread(nullptr, 0, &WorkerThreadProc, nullptr, 0, nullptr);
        if (!thread) {
            LOG_ERROR("proxy: CreateThread failed, gle={}", GetLastError());
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            FreeLibrary(g_selfModule);
            g_selfModule = nullptr;
            break;
        }
        // Fire-and-forget: nothing waits on this thread (see the deadlock
        // note in WorkerThreadProc above), so there is no reason to keep
        // its handle open.
        CloseHandle(thread);
        break;
    }
    case DLL_PROCESS_DETACH:
        // reserved != nullptr means the process itself is terminating: every
        // thread is already gone or being force-torn-down by the OS, so
        // there is nothing useful to signal. On a real, explicit
        // FreeLibrary unload, just signal the worker thread to stop — never
        // wait on it here (see WorkerThreadProc).
        if (reserved == nullptr && g_stopEvent) {
            LOG_INFO("MediaDB64 proxy detaching, signaling worker thread to stop");
            SetEvent(g_stopEvent);
        } else {
            LOG_INFO("MediaDB64 proxy detached (process exit)");
        }
        break;
    default:
        break;
    }
    return TRUE;
}
