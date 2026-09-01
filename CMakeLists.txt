cmake_minimum_required(VERSION 3.16)

project(RetroDcsDemo VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Threads REQUIRED)

add_executable(retro_dcs_demo
    src/main.cpp
    src/mainwindow.cpp
    src/retro_style.cpp
    src/manager_client.cpp
    src/project_metadata.cpp
    src/csv_utils.cpp
    src/modbus_shared_memory_reader.cpp
)

target_link_libraries(retro_dcs_demo PRIVATE
    Qt5::Widgets
    Qt5::Network
    Qt5::Sql
    Threads::Threads
)

target_compile_definitions(
    retro_dcs_demo
    PRIVATE
    RETRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
)