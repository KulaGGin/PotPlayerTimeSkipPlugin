# Vendored Catch2

`catch_amalgamated.hpp` / `catch_amalgamated.cpp` are Catch2 v3.7.1's
official amalgamated distribution (all of Catch2 collapsed into one header +
one source file), downloaded unmodified from the
[v3.7.1 release assets](https://github.com/catchorg/Catch2/releases/tag/v3.7.1).
`cmake/Catch.cmake`, `cmake/CatchAddTests.cmake`, and
`cmake/CatchShardTests.cmake` are the matching CTest-integration scripts
from the same tag's `extras/` folder — they're what makes
`catch_discover_tests()` register every `TEST_CASE` as its own CTest test.
`LICENSE.txt` is Catch2's own license (Boost Software License 1.0).

Vendoring instead of `FetchContent`/a package manager means `cmake -B
build` never needs network access and the version in use is always exactly
what's checked in — no floating tag, no build-time surprise.

## Upgrading

1. Pick a new release tag from <https://github.com/catchorg/Catch2/releases>.
2. Replace `catch_amalgamated.hpp` and `catch_amalgamated.cpp` with that
   release's copies (release assets, or `extras/` in the tagged source
   tree).
3. Replace `cmake/Catch.cmake`, `cmake/CatchAddTests.cmake`, and
   `cmake/CatchShardTests.cmake` with that tag's `extras/` copies.
4. Update the version number in this file and in the comment atop
   `../../tests/README.md`'s Framework section.
5. Rebuild and run the full suite (see `../../tests/README.md`) before
   committing the bump.
