add_library(luma_core STATIC
    types.cpp
    inproc_event_bus.cpp
)

add_library(luma::core ALIAS luma_core)

target_include_directories(luma_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(luma_core
    PUBLIC
        Threads::Threads
    PRIVATE
        luma_compile_options
)
