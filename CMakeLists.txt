cmake_minimum_required(VERSION 3.16)
project(problem_029 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(code
    main.c
    allocator.c
)

target_compile_options(code PRIVATE -Wall -Wextra -Wpedantic)
