cmake_minimum_required(VERSION 3.16)
project(luma VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LUMA_BUILD_TESTS "Build unit tests" ON)
option(LUMA_BUILD_TOOLS "Build hardware probe tools" ON)
option(LUMA_ENABLE_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(LUMA_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(LUMA_ENABLE_ASAN AND LUMA_ENABLE_TSAN)
    message(FATAL_ERROR "LUMA_ENABLE_ASAN and LUMA_ENABLE_TSAN cannot be enabled simultaneously.")
endif()

find_package(Threads REQUIRED)

add_library(luma_compile_options INTERFACE)
target_compile_options(luma_compile_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

if(LUMA_ENABLE_ASAN)
    target_compile_options(luma_compile_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(luma_compile_options INTERFACE -fsanitize=address,undefined)
elseif(LUMA_ENABLE_TSAN)
    target_compile_options(luma_compile_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(luma_compile_options INTERFACE -fsanitize=thread)
endif()

# 严格无环 DAG：core -> safety / hal -> execution
add_subdirectory(src/core)
add_subdirectory(src/safety)
add_subdirectory(src/hal)
add_subdirectory(src/execution)

if(LUMA_BUILD_TOOLS)
    add_executable(luma_probe tools/luma_probe.cpp)
    target_link_libraries(luma_probe PRIVATE luma::execution luma_compile_options)

    add_executable(luma_move_demo tools/luma_move_demo.cpp)
    target_link_libraries(luma_move_demo PRIVATE luma::execution luma_compile_options)
endif()

if(LUMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
