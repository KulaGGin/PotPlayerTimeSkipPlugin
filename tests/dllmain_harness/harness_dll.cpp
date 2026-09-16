#include <windows.h>

#include "diagnostics/log.hpp"

// Logs from DllMain itself, on both attach and detach, including a
// non-ASCII message — this is the harness for PTS-004's "does not deadlock
// when exercised from a DllMain-like context" acceptance criterion.
// dllmain_harness_runner.cpp LoadLibrary/FreeLibrary's this DLL under a
// CTest-enforced timeout: if the logging path ever contended the loader
// lock, that call would hang and the test would time out instead of
// passing silently.
BOOL APIENTRY DllMain(HMODULE /*module*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        LOG_INFO("dllmain harness attach (non-ASCII check: caf\xC3\xA9, \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, intro \xE2\x80\x93 outro)");
        break;
    case DLL_PROCESS_DETACH:
        LOG_WARN("dllmain harness detach");
        break;
    default:
        break;
    }
    return TRUE;
}
