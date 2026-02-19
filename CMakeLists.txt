cmake_minimum_required(VERSION 3.16)
project(Layout2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(TCL REQUIRED)

qt_standard_project_setup()

qt_add_executable(layout2
    src/main.cpp
    src/TclConsoleWindow.cpp
    src/TclConsoleWindow.h
    src/LayerManager.cpp
    src/LayerManager.h
    src/LayoutEditorWindow.cpp
    src/LayoutEditorWindow.h
)

target_include_directories(layout2 PRIVATE ${TCL_INCLUDE_PATH})
target_link_libraries(layout2 PRIVATE Qt6::Widgets ${TCL_LIBRARY})
