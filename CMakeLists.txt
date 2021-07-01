cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
SET(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

# set(PROJECT simg_ld)
# project(${PROJECT} LANGUAGES CXX C)
project(simg_ld LANGUAGES CXX C)

FIND_PATH(LIBEVENT_INCLUDE_DIR NAMES event2/event.h)
# FIND_PACKAGE(libevent REQUIRED)
FIND_LIBRARY(LIBEVENT_LIBRARY NAMES event)
SET(LIBEVENT_LIBRARIES "${LIBEVENT_LIBRARY}")
# INCLUDE_DIRECTORIES(${LIBEVENT_INCLUDE_DIR})
# MESSAGE(STATUS "Found libevent2 includes: ${LIBEVENT2_INCLUDE_DIR}/event2/event.h")

add_executable(simg_ld 
    simg_ld.cpp
    loader.cpp
    loader.h
    switch_context.h
    switch_context.cpp    
    )

# add_executable(${PROJECT} ${SOURCES})
# add_executable(simg_ld ${SOURCES})
# target_link_libraries(${PROJECT} ${LIBEVENT_LIBRARIES})
target_link_libraries(simg_ld ${LIBEVENT_LIBRARIES})
# target_link_libraries(${PROJECT} -static)

add_executable(app_loader 
    app_loader_main.cpp
    app_loader.h
    app_loader.cpp
    app_loader_global.hpp)
