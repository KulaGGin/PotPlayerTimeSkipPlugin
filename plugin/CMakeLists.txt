add_library(plugin STATIC
    src/plugin.cpp
)

target_include_directories(plugin PUBLIC include)
target_link_libraries(plugin PUBLIC core diagnostics)
