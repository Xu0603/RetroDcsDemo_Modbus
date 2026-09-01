cmake_minimum_required(VERSION 3.16)

project(PythonCommunicationDemo VERSION 0.1 LANGUAGES CXX)


# ============================================================
# C++ Standard
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ============================================================
# Dependencies
# ============================================================

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)


# ============================================================
# libmodbus
# ============================================================

find_path(
    MODBUS_INCLUDE_DIR
    NAMES modbus/modbus.h
    PATHS
        /usr/include
        /usr/local/include
)

find_library(
    MODBUS_LIBRARY
    NAMES modbus
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
)

if(NOT MODBUS_INCLUDE_DIR)
    message(FATAL_ERROR "libmodbus header not found")
endif()

if(NOT MODBUS_LIBRARY)
    message(FATAL_ERROR "libmodbus library not found")
endif()

message(STATUS "Found libmodbus include: ${MODBUS_INCLUDE_DIR}")
message(STATUS "Found libmodbus library: ${MODBUS_LIBRARY}")


# ============================================================
# System Core
# ============================================================

add_library(system_core

    # Existing modules
    manager/config_repository.cpp
    manager/shared_memory.cpp
    manager/ipc_protocol.cpp
    manager/shm_ipc.cpp
    manager/tcp_server.cpp
    manager/runtime_logger.cpp

    # Modbus modules
    manager/modbus/modbus_client.cpp
    manager/modbus/modbus_codec.cpp
    manager/modbus/modbus_manager.cpp
)


# ============================================================
# Include Directories
# ============================================================

target_include_directories(
    system_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MODBUS_INCLUDE_DIR}
)


# ============================================================
# Link Libraries
# ============================================================

target_link_libraries(
    system_core
    PUBLIC
        SQLite::SQLite3
        Threads::Threads
        ${MODBUS_LIBRARY}
)


# ============================================================
# Linux realtime library
# ============================================================

if(UNIX AND NOT APPLE)

    target_link_libraries(
        system_core
        PUBLIC
            rt
    )

endif()


# ============================================================
# Compiler Warnings
# ============================================================

target_compile_options(
    system_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)


# ============================================================
# System Manager
# ============================================================

add_executable(
    system-managerd
    manager/main.cpp
)

target_link_libraries(
    system-managerd
    PRIVATE
        system_core
)


# ============================================================
# Component Tests
# ============================================================

add_executable(
    component_tests
    tests/component_tests.cpp
)

target_link_libraries(
    component_tests
    PRIVATE
        system_core
)


add_executable(modbus_codec_test tests/modbus_codec_test.cpp)
target_link_libraries(modbus_codec_test PRIVATE system_core)


# ============================================================
# Modbus Client Test
# ============================================================

add_executable(
    modbus_client_test
    tests/modbus_client_test.cpp
)

target_link_libraries(
    modbus_client_test
    PRIVATE
        system_core
)


# ============================================================
# CTest
# ============================================================

enable_testing()

add_test(
    NAME component_tests
    COMMAND
        component_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/database/demo.sqlite
)

add_test(NAME modbus_codec_test
    COMMAND modbus_codec_test)