# Vendored Catch2 v3.7.1 (amalgamated single-header/single-source
# distribution) — see README.md in this directory for why it's vendored
# rather than fetched. Upstream: https://github.com/catchorg/Catch2

add_library(Catch2WithMain STATIC
    catch_amalgamated.cpp
)

target_include_directories(Catch2WithMain PUBLIC .)
target_compile_features(Catch2WithMain PUBLIC cxx_std_17)

add_library(Catch2::Catch2WithMain ALIAS Catch2WithMain)

# Exposes catch_discover_tests() to whoever adds this subdirectory.
set(CATCH2_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" PARENT_SCOPE)
