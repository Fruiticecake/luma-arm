add_library(luma_hal STATIC
    posix_serial_transport.cpp
    tcp_transport.cpp
)

add_library(luma::hal ALIAS luma_hal)

target_include_directories(luma_hal PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(luma_hal
    PUBLIC
        luma::core
    PRIVATE
        luma_compile_options
)
