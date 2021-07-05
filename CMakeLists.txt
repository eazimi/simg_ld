cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
SET(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

PROJECT(simg_ld LANGUAGES CXX C)

add_executable(simg_ld 
    simg_ld_main.cpp
    loader.cpp
    loader.h
    switch_context.h
    switch_context.cpp    
    )
target_link_libraries(simg_ld -static)

add_executable(app_loader 
    app_loader_main.cpp
    app_loader.h
    app_loader.cpp
    app_loader_global.hpp)
