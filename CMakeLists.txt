cmake_minimum_required(VERSION 3.16)
project(Layout2 LANGUAGES CXX)

# Require a modern C++ dialect consistently across translation units.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt meta-object auto-processing for QObject/Q_OBJECT usage.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Tcl C API is mandatory because command execution is core architecture.
find_package(TCL REQUIRED)

# Ubuntu commonly ships Qt5 by default; prefer Qt6 if present.
find_package(Qt6 QUIET COMPONENTS Widgets)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

# Application sources.
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

# Include Tcl headers and link both Qt widgets and Tcl library.
target_include_directories(layout2 PRIVATE ${TCL_INCLUDE_PATH})
target_link_libraries(layout2 PRIVATE ${QT_PACKAGE}::Widgets ${TCL_LIBRARY})

# Copy runtime data/scripts into build directory for startup loading.
configure_file(data/example_layers.txt ${CMAKE_CURRENT_BINARY_DIR}/example_layers.txt COPYONLY)
configure_file(scripts/init.tcl ${CMAKE_CURRENT_BINARY_DIR}/init.tcl COPYONLY)
