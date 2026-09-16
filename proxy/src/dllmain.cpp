#include <windows.h>

#include "diagnostics/log.hpp"

// Stub proxy DLL. Forwards nothing yet — real export forwarding to a
// renamed MediaDB64_orig.dll lands in PTS-006, gated on the PTS-005
// viability spike.
//
// Logging directly from DllMain here is deliberate: it is the earliest and
// riskiest point in the plugin's lifetime (see PTS-004 / diagnostics/), so
// it doubles as a live smoke test that the logging path is safe to call
// from exactly this context.
BOOL APIENTRY DllMain(HMODULE /*module*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        LOG_INFO("MediaDB64 proxy attached");
        break;
    case DLL_PROCESS_DETACH:
        LOG_INFO("MediaDB64 proxy detached");
        break;
    default:
        break;
    }
    return TRUE;
}
