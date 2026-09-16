add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/catch2 ${CMAKE_CURRENT_BINARY_DIR}/catch2)

add_executable(unit_tests
    unit/core/add_test.cpp
    unit/core/timecode_test.cpp
    unit/core/skip_range_test.cpp
    unit/core/pbf_test.cpp
    unit/core/human_input_test.cpp
    unit/diagnostics/format_test.cpp
    unit/plugin/window_test.cpp
)

target_link_libraries(unit_tests PRIVATE core diagnostics plugin Catch2::Catch2WithMain)

include(${CATCH2_MODULE_PATH}/Catch.cmake)
catch_discover_tests(unit_tests)

# Not a Catch2 case: a real LoadLibrary/FreeLibrary round trip against a DLL
# that logs from its own DllMain, registered directly as a CTest test.
add_subdirectory(dllmain_harness)

# PTS-006: the same LoadLibrary/FreeLibrary round trip, but against the
# actual production proxy DLL, to verify its DllMain worker-thread
# bootstrap/teardown doesn't deadlock. PE export forwarders resolve lazily
# (only when an export is actually looked up via GetProcAddress), so this
# loads fine in CI without a real MediaDB64_orig.dll present.
add_test(NAME proxy_dllmain_no_deadlock
         COMMAND dllmain_harness_runner $<TARGET_FILE:MediaDB64>)
set_tests_properties(proxy_dllmain_no_deadlock PROPERTIES TIMEOUT 15)
