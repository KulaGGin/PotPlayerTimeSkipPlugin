add_library(dllmain_harness SHARED harness_dll.cpp)
target_link_libraries(dllmain_harness PRIVATE diagnostics)

add_executable(dllmain_harness_runner runner.cpp)

add_test(NAME dllmain_harness_no_deadlock
         COMMAND dllmain_harness_runner $<TARGET_FILE:dllmain_harness>)

# A deadlocked loader lock would otherwise hang ctest indefinitely instead
# of failing the test.
set_tests_properties(dllmain_harness_no_deadlock PROPERTIES TIMEOUT 15)
