cmake_minimum_required(VERSION 3.20)

project(vtracer_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vtracer_core
    src/stb_image_impl.cpp
    src/vtracer.cpp
)

target_include_directories(vtracer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
)

if(MSVC)
    target_compile_options(vtracer_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(vtracer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(vtracer_native SHARED
    src/vtracer_c_api.cpp
)

target_link_libraries(vtracer_native PRIVATE vtracer_core)

target_include_directories(vtracer_native
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(vtracer_native PRIVATE /W4 /permissive-)
else()
    target_compile_options(vtracer_native PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vtracer_cli
    app/main.cpp
)

target_link_libraries(vtracer_cli PRIVATE vtracer_core)
