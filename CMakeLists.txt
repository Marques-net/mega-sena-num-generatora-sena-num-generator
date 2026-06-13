cmake_minimum_required(VERSION 3.16)

project(mega_sena_num_generator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(mega_sena_num_generator
    src/main.cpp
    src/DEMSolver.cpp
    src/GradientCalibrator.cpp
    src/HttpSimulationServer.cpp
    src/LotteryMachine.cpp
)

add_executable(mega_sena_calibration_agent
    src/CalibrationAgent.cpp
    src/DEMSolver.cpp
    src/GradientCalibrator.cpp
    src/LotteryMachine.cpp
)

target_include_directories(mega_sena_num_generator PRIVATE src)
target_include_directories(mega_sena_calibration_agent PRIVATE src)
target_link_libraries(mega_sena_num_generator PRIVATE Threads::Threads)
target_link_libraries(mega_sena_calibration_agent PRIVATE Threads::Threads)

if (MSVC)
    target_compile_options(mega_sena_num_generator PRIVATE /W4)
    target_compile_options(mega_sena_calibration_agent PRIVATE /W4)
else()
    target_compile_options(mega_sena_num_generator PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mega_sena_calibration_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()
