cmake_minimum_required(VERSION 3.20)
project(horizonroads VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia)

add_executable(horizonroads
    src/main.cpp
    src/RoadAudio.h
    src/RoadAudio.cpp
    src/WorldSim.h
    src/WorldSim.cpp
    src/RoadView.h
    src/RoadView.cpp
)

target_compile_options(horizonroads PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(horizonroads PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
    Qt5::Multimedia
    GLESv2
)
