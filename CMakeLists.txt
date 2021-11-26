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

add_executable(app
    app_main.cpp
    app.h
    app.cpp
    global.hpp
    channel.hpp
    channel.cpp)

add_executable(mc 
    mc_main.cpp
    dyn_obj_info.hpp
    channel.hpp
    channel.cpp
    global.hpp
    sync_proc.hpp
    sync_proc.cpp
    cmd_args.hpp
    memory_map.h
    memory_map.cpp
    stack.h
    stack.cpp
    heap.hpp
    user_space.h
    user_space.cpp
    app_loader.h
    app_loader.cpp
    trampoline.h
    trampoline.cpp
    trampoline_wrappers.hpp
    switch_context.h
    switch_context.cpp
    mc.h
    mc.cpp
    cmdline_params.h
    cmdline_params.cpp
    )
find_library(LIBEVENT_LIBRARY NAMES event)    
target_link_libraries(mc ${LIBEVENT_LIBRARY})

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_subdirectory(${simgld_SOURCE_DIR}/sgld)