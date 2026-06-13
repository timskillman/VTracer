cmake_minimum_required(VERSION 3.16)

project(svg2obj VERSION 1.0.0 LANGUAGES CXX)

add_executable(svg2obj src/main.cpp)
target_compile_features(svg2obj PRIVATE cxx_std_20)
target_include_directories(svg2obj PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/delaunator/include")

if(MSVC)
  target_compile_options(svg2obj PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(svg2obj PRIVATE -Wall -Wextra -Wpedantic)
endif()

