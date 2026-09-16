add_library(diagnostics STATIC
    src/format.cpp
    src/log.cpp
)

target_include_directories(diagnostics PUBLIC include)

if(MSVC)
    # Source files contain literal non-ASCII text (log messages, tests);
    # /utf-8 makes MSVC treat both source and narrow-string-literal
    # execution charset as UTF-8 regardless of the build machine's ANSI
    # codepage.
    target_compile_options(diagnostics PUBLIC /utf-8)
endif()
