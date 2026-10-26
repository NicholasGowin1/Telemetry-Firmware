cmake_minimum_required(VERSION 3.20)
project(TelemetryDashFirmware LANGUAGES C CXX)

# Enforce standard multi-platform C++ compiler settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Locate Python 3 for the dashboard and standalone utilities
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 2. Automated sanity check for contributors cloning the repo
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pygame"
    RESULT_VARIABLE PYGAME_MISSING
    OUTPUT_QUIET ERROR_QUIET
)

if(PYGAME_MISSING)
    message(WARNING "Pygame is missing! The 'Dash' dashboard will not run. "
                    "Please check your README.md or execute: pip install pygame")
endif()

# 3. Include subdirectories for compilation
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CAN_disect/CAN_Tools/CMakeLists.txt")
    add_subdirectory(CAN_disect/CAN_Tools)
else()
    message(FATAL_ERROR "CAN_Tools build directory missing configuration!")
endif()