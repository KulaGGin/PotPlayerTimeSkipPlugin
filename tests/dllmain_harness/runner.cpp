#include <windows.h>

#include <cstdio>

// Loads and unloads dllmain_harness.dll, which logs from inside its own
// DllMain. Registered as a CTest test with a TIMEOUT (see CMakeLists.txt):
// if the logging path ever deadlocked under the loader lock, LoadLibrary
// would hang here and ctest would report a timeout failure instead of the
// process just never returning.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dllmain_harness_runner <harness dll path>\n");
        return 2;
    }

    const HMODULE module = LoadLibraryA(argv[1]);
    if (!module) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    if (!FreeLibrary(module)) {
        std::fprintf(stderr, "FreeLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    return 0;
}
