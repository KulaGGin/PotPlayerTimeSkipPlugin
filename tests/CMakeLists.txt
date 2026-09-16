add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/catch2 ${CMAKE_CURRENT_BINARY_DIR}/catch2)

add_executable(unit_tests
    unit/core/add_test.cpp
    unit/diagnostics/format_test.cpp
)

target_link_libraries(unit_tests PRIVATE core diagnostics Catch2::Catch2WithMain)

include(${CATCH2_MODULE_PATH}/Catch.cmake)
catch_discover_tests(unit_tests)

# Not a Catch2 case: a real LoadLibrary/FreeLibrary round trip against a DLL
# that logs from its own DllMain, registered directly as a CTest test.
add_subdirectory(dllmain_harness)
