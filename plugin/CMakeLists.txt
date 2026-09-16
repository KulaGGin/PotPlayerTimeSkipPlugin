add_library(plugin STATIC
    src/plugin.cpp
    src/window.cpp
    src/player_window.cpp
)

target_include_directories(plugin PUBLIC include)
target_link_libraries(plugin PUBLIC core diagnostics)
