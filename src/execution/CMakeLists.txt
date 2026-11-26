add_library(luma_execution STATIC
    nexarm_protocol.cpp
    nexarm_driver.cpp
)

add_library(luma::execution ALIAS luma_execution)

target_include_directories(luma_execution PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(luma_execution
    PUBLIC
        luma::core
        luma::safety
        luma::hal
    PRIVATE
        luma_compile_options
)
