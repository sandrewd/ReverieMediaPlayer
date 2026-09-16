cmake_minimum_required(VERSION 3.22)
project(player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick OpenGL)
qt_standard_project_setup(REQUIRES 6.4)

# projectM is vendored and built into build/projectm-install (see README).
find_package(projectM4 REQUIRED CONFIG
    PATHS "${CMAKE_SOURCE_DIR}/build/projectm-install/lib/cmake/projectM4"
    NO_DEFAULT_PATH)

qt_add_executable(player
    src/main.cpp
    src/ProjectMItem.cpp
    src/ProjectMItem.h
)

qt_add_qml_module(player
    URI Player
    VERSION 1.0
    QML_FILES qml/main.qml
)

target_include_directories(player PRIVATE src)

target_link_libraries(player PRIVATE
    Qt6::Quick
    Qt6::OpenGL
    libprojectM::projectM
)
