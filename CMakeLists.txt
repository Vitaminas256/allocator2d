cmake_minimum_required(VERSION 3.30)
project(allocator2d)


option(ENABLE_TEST "Enable test mode checks" OFF)

if(ENABLE_TEST)
    set(CMAKE_CXX_STANDARD 23)
else ()
    set(CMAKE_CXX_STANDARD 20)
endif ()

add_executable(allocator2d main.cpp)

if(ENABLE_TEST)
    target_compile_definitions(allocator2d PRIVATE ENABLE_TEST)
endif()