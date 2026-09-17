add_library(plugin STATIC
    src/window.cpp
    src/window_enum.cpp
    src/player_window.cpp
    src/skip_setup.cpp
    src/hotkeys.cpp
    src/hotkey_pump.cpp
)

target_include_directories(plugin PUBLIC include)
target_link_libraries(plugin PUBLIC core diagnostics)
