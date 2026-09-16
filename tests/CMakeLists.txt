add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/catch2 ${CMAKE_CURRENT_BINARY_DIR}/catch2)

add_executable(unit_tests
    unit/core/add_test.cpp
)

target_link_libraries(unit_tests PRIVATE core Catch2::Catch2WithMain)

include(${CATCH2_MODULE_PATH}/Catch.cmake)
catch_discover_tests(unit_tests)
