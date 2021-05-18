cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
SET(CMAKE_COLOR_MAKEFILE ON)

set(PROJECT simg_ld)
project(${PROJECT} LANGUAGES CXX C)

set(SOURCES 
    main.cpp
    ulexec.h
    ulexec.cpp
    util.hpp
    loader.cpp
    loader.h
    switch_context.h
    switch_context.cpp
    )

add_executable(${PROJECT} ${SOURCES})
target_link_libraries(${PROJECT} -static)
