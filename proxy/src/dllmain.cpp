#include <windows.h>

// Stub proxy DLL. Forwards nothing yet — real export forwarding to a
// renamed MediaDB64_orig.dll lands in PTS-006, gated on the PTS-005
// viability spike.
BOOL APIENTRY DllMain(HMODULE /*module*/, DWORD /*reason*/, LPVOID /*reserved*/) {
    return TRUE;
}
