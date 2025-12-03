cmake_minimum_required(VERSION 4.0)
project(allocator2d)

set(CMAKE_CXX_STANDARD 23)

add_executable(allocator2d main.cpp)

target_include_directories(allocator2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
