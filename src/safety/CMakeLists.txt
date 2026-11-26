add_library(luma_safety STATIC
    safety_guard.cpp
)

add_library(luma::safety ALIAS luma_safety)

target_include_directories(luma_safety PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(luma_safety
    PUBLIC
        luma::core
    PRIVATE
        luma_compile_options
)
