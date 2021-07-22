cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

project(simg_ld LANGUAGES CXX C)

add_executable(app_loader 
    app_loader_main.cpp
    app_loader.h
    app_loader.cpp
    global.hpp)

add_executable(simg_ld 
    simg_ld_main.cpp
    loader.cpp
    loader.h
    dyn_obj_info.hpp
    channel.hpp
    channel.cpp
    global.hpp
    loader_global_funcs.hpp
    sync_proc.hpp
    sync_proc.cpp
    )
target_link_libraries(simg_ld ${CMAKE_SOURCE_DIR}/libevent_core.a -static)