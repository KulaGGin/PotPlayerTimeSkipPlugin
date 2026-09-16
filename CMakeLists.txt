cmake_minimum_required(VERSION 3.21)
project(PotPlayerTimeSkipPlugin CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project targets x64 only. Configure with -A x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # Keep debug info (and thus .pdb) even in optimized Release builds —
    # we need symbols to debug crashes inside PotPlayer.
    add_compile_options(/W4 /Zi)
    add_link_options(/DEBUG /OPT:REF /OPT:ICF)
endif()

enable_testing()

add_subdirectory(core)
add_subdirectory(diagnostics)
add_subdirectory(plugin)
add_subdirectory(proxy)
add_subdirectory(tests)
