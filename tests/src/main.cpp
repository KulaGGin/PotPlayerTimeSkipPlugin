#include <cstdio>
#include <cstdlib>

#include "core/core.hpp"

// Minimal, framework-free test runner. A real harness arrives with
// PTS-003; this only has to prove the test binary compiles, links
// against core, and is discovered/run by the documented command.
#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAILED: %s (%s:%d)\n", #cond, __FILE__,    \
                          __LINE__);                                         \
            ++failed;                                                        \
        } else {                                                             \
            ++passed;                                                        \
        }                                                                    \
    } while (0)

int main() {
    int passed = 0;
    int failed = 0;

    ASSERT(1 + 1 == 2);
    ASSERT(core::add(1, 1) == 2);

    std::printf("unit_tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
