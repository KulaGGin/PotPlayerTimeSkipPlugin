add_library(MediaDB64 SHARED
    src/dllmain.cpp
)

set_target_properties(MediaDB64 PROPERTIES OUTPUT_NAME "MediaDB64")
target_link_libraries(MediaDB64 PRIVATE plugin diagnostics)
