cmake_minimum_required(VERSION 3.10)
project(fsae_telemetry CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# can_bus.cpp uses std::thread / std::mutex / std::condition_variable.
find_package(Threads REQUIRED)

# Everything both monitor and simulator need: the DBC parser, the raw-text
# BO_/VAL_ scanners, the hash table, the CAN bus abstraction, and the
# shared simulate() used by both --demo modes.
add_library(dbc_core STATIC
    dbc_parser.cpp
    dbc_scan.cpp
    dbc_val_parser.cpp
    msg_hash_table.cpp
    can_bus.cpp
    Simulator.cpp
)
target_include_directories(dbc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dbc_core PUBLIC Threads::Threads)

add_executable(monitor Monitor.cpp)
target_link_libraries(monitor PRIVATE dbc_core)

add_executable(simulator simulator_main.cpp)
target_link_libraries(simulator PRIVATE dbc_core)