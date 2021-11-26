cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(BUILD_SHARED_LIBS ON)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

project(simgld C CXX)

add_library(wrapper SHARED   
    wrapper.cpp
)
set_property(TARGET wrapper PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wrapper ${CMAKE_DL_LIBS})

add_subdirectory(${simgld_SOURCE_DIR}/app)
add_subdirectory(${simgld_SOURCE_DIR}/mc)
add_subdirectory(${simgld_SOURCE_DIR}/sgld)

