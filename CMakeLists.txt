cmake_minimum_required(VERSION 3.16)

project(mega_sena_num_generator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mega_sena_num_generator
    src/main.cpp
    src/DEMSolver.cpp
    src/LotteryMachine.cpp
)

target_include_directories(mega_sena_num_generator PRIVATE src)

if (MSVC)
    target_compile_options(mega_sena_num_generator PRIVATE /W4)
else()
    target_compile_options(mega_sena_num_generator PRIVATE -Wall -Wextra -Wpedantic)
endif()

