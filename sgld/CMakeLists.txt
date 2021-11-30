set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_BUILD_TYPE Debug)

include_directories(${simgld_SOURCE_DIR}/include)

add_executable(sgld
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
    main.cpp
)
target_link_libraries(sgld -static)