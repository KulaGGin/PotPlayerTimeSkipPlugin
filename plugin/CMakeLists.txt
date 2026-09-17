add_library(plugin STATIC
    src/window.cpp
    src/window_enum.cpp
    src/player_window.cpp
    src/skip_setup.cpp
    src/osd.cpp
    src/skip_marking.cpp
    src/hotkeys.cpp
    src/hotkey_pump.cpp
    src/config.cpp
    src/self_check.cpp
)

target_include_directories(plugin PUBLIC include)
target_link_libraries(plugin PUBLIC core diagnostics version)
