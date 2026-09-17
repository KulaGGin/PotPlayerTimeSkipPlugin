add_library(core STATIC
    src/core.cpp
    src/config.cpp
)

target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC diagnostics)
