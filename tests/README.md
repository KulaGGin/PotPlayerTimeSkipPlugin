# tests

`unit_tests` — the native unit-test suite, built and run via CTest.

`tests/integration/` — PTS-018's **opt-in** end-to-end suite against a live
PotPlayer, verifying the `.pbf` on disk. Not part of CTest or the fast unit
run; see [`tests/integration/README.md`](integration/README.md) for
prerequisites and how to run it.

## Framework

[Catch2](https://github.com/catchorg/Catch2) v3.7.1, vendored as its
amalgamated single-header/single-source distribution under
[`third_party/catch2/`](../third_party/catch2). It's checked into the repo
rather than fetched at configure time, so `cmake -B build` never needs
network access and the vendored version never drifts under you between
builds. See [`third_party/catch2/README.md`](../third_party/catch2/README.md)
for upgrade instructions.

## Layout

`tests/unit/` mirrors the top-level module it tests, one file per source
file under test:

```
core/include/core/core.hpp   ->  tests/unit/core/core_test.cpp
core/src/core.cpp            ->
```

(Today that's `tests/unit/core/add_test.cpp` for `core::add`, and
`tests/unit/diagnostics/format_test.cpp` for the diagnostics module's pure
line-formatting logic.)

## The DllMain harness

`tests/dllmain_harness/` is a separate, non-Catch2 CTest test: a real
`dllmain_harness.dll` that calls `LOG_*` from inside its own `DllMain`, and
a tiny `dllmain_harness_runner.exe` that `LoadLibrary`/`FreeLibrary`s it.
It exists because the diagnostics module (PTS-004) is explicitly required
to be safe to call from a `DllMain`-like context — a deadlocked loader lock
can't be caught by a normal assertion, but it will hang `LoadLibrary`
forever, so the test is registered with a CTest `TIMEOUT` (see
`tests/dllmain_harness/CMakeLists.txt`) that turns a hang into a failure
instead of an indefinitely stuck `ctest` run.

## Adding a test

Test files are auto-discovered — there is no list to edit. Drop a new
`*_test.cpp` under `tests/unit/`, mirroring the path of the module it
covers, add it to the `add_executable(unit_tests ...)` source list in
[`CMakeLists.txt`](CMakeLists.txt)`, `#include <catch_amalgamated.hpp>`, and
write `TEST_CASE`s. Re-run CMake configure once so `catch_discover_tests`
picks up any new `TEST_CASE`s as individual CTest tests; no other wiring is
needed.

For parsing/formatting-style logic (timecodes, `.pbf` fields, ...), prefer a
table-driven `TEST_CASE` with `GENERATE` over one `TEST_CASE` per input —
see `add_test.cpp` for the pattern. Pair every positive case with at least
one negative case that a broken implementation would actually fail (a test
that can't fail is worse than no test).

## Build and run

```
cmake -B build -A x64
cmake --build build --config Release --target unit_tests
ctest --test-dir build -C Release --output-on-failure
```

Each `TEST_CASE` shows up as its own CTest test (`ctest -N` to list them),
so `ctest -R <name>` runs a single one. The whole suite runs in well under a
second.

A non-zero `ctest`/`unit_tests.exe` exit code means at least one assertion
failed; `ctest --output-on-failure` prints the failing `REQUIRE`/`CHECK`,
file, and line.

## Verifying the harness actually catches failures

Since a test suite that can't fail is worse than no suite, don't just trust
that it's wired up — prove it once after touching the harness itself:

1. Temporarily change an expected value in `add_test.cpp` (e.g. `Case{1, 1,
   2}` -> `Case{1, 1, 3}`).
2. Rebuild and run `ctest` — confirm it reports a failure (red), naming the
   file/line/values, and that `unit_tests.exe` exits non-zero.
3. Revert the change, rebuild, and run `ctest` again — confirm it's back to
   all-green with exit code 0.

This is exactly what was done while building this harness (see the PR
description for the captured red/green transcript); it's not something that
ships as a permanently-failing test in the suite.
