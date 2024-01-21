cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# For profiling
# SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
# SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
# SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")

project("ECS")

add_executable(ecs
    src/main.cpp
    src/ecs.h
    src/nostd.h
)

find_package(SDL2 REQUIRED)

target_link_libraries(ecs PRIVATE $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)

# set_property(TARGET ecs PROPERTY CXX_CLANG_TIDY "clang-tidy" "--fix")
# set_property(TARGET ecs PROPERTY CXX_INCLUDE_WHAT_YOU_USE "iwyu-tool")

target_compile_options(ecs PRIVATE -Wall -Wextra -fdiagnostics-color=always -ggdb -march=native)
# target_compile_options(ecs PRIVATE -fsanitize=address)
# target_link_options(ecs PRIVATE -fsanitize=address)
