#include <windows.h>

#include "diagnostics/log.hpp"

// PTS-005 viability-spike proxy. All three real exports are forwarded to
// MediaDB64_orig.dll (PE forwarder RVAs, resolved by the loader — none of
// this file's own code runs on the call path) via linker /EXPORT switches
// injected below. This DllMain carries no plugin logic by design; that
// lands in PTS-006 once this spike confirms PotPlayer accepts an unsigned
// proxy.
//
// Export names are measured via `dumpbin /exports` — see
// docs/FINDINGS.md §5. The real MediaDB64.dll must be renamed to
// MediaDB64_orig.dll and live next to this proxy for these to resolve.
#pragma comment(linker, "/EXPORT:CreateDatabaseEngine=MediaDB64_orig.CreateDatabaseEngine")
#pragma comment(linker, "/EXPORT:CreateJpegDecoder=MediaDB64_orig.CreateJpegDecoder")
#pragma comment(linker, "/EXPORT:CreateSMTC=MediaDB64_orig.CreateSMTC")

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
