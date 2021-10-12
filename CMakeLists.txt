cmake_minimum_required(VERSION 3.5)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

project(simg_ld LANGUAGES CXX C)
find_library(LIBEVENT_LIBRARY NAMES event)

add_executable(app
    app_main.cpp
    app.h
    app.cpp
    global.hpp
    channel.hpp
    channel.cpp)

add_executable(parentProc 
    parent_proc_main.cpp
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
    parent_proc.h
    parent_proc.cpp
    cmdline_params.h
    cmdline_params.cpp
    )
target_link_libraries(parentProc ${LIBEVENT_LIBRARY})    

add_executable(simgld
    stack.h
    stack.cpp
    heap.hpp
    user_space.h
    user_space.cpp
    app_loader.h
    app_loader.cpp
    main.cpp
)
target_link_libraries(simgld -static)