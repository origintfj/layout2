cmake_minimum_required(VERSION 3.16)
project(Layout2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(TCL REQUIRED)

# Ubuntu LTS versions commonly ship Qt5 by default; prefer Qt6 when available,
# but gracefully fall back to Qt5.
find_package(Qt6 QUIET COMPONENTS Widgets)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

set(LAYOUT2_SOURCES
    src/main.cpp
    src/TclConsoleWindow.cpp
    src/TclConsoleWindow.h
    src/LayerManager.cpp
    src/LayerManager.h
    src/LayoutEditorWindow.cpp
    src/LayoutEditorWindow.h
)

add_executable(layout2 ${LAYOUT2_SOURCES})

target_include_directories(layout2 PRIVATE ${TCL_INCLUDE_PATH})
target_link_libraries(layout2 PRIVATE ${QT_PACKAGE}::Widgets ${TCL_LIBRARY})

configure_file(data/example_layers.txt ${CMAKE_CURRENT_BINARY_DIR}/example_layers.txt COPYONLY)
configure_file(scripts/init.tcl ${CMAKE_CURRENT_BINARY_DIR}/init.tcl COPYONLY)
