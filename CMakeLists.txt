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
find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
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
    src/SelectionPropertiesDialog.cpp
    src/SelectionPropertiesDialog.h
    src/LayoutSceneModel.cpp
    src/LayoutSceneModel.h
    src/LayoutGeometry.h
    src/EditorSessionController.cpp
    src/EditorSessionController.h
)

add_executable(layout2 ${LAYOUT2_SOURCES})

# Include Tcl headers and link both Qt widgets and Tcl library.
target_include_directories(layout2 PRIVATE ${TCL_INCLUDE_PATH})
if (QT_PACKAGE STREQUAL "Qt6")
    target_link_libraries(layout2 PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::OpenGLWidgets ${TCL_LIBRARY})
else()
    target_link_libraries(layout2 PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::OpenGL ${TCL_LIBRARY})
endif()

# Copy runtime data/scripts into build directory for startup loading.
configure_file(data/example_layers.txt ${CMAKE_CURRENT_BINARY_DIR}/example_layers.txt COPYONLY)
configure_file(scripts/init.tcl ${CMAKE_CURRENT_BINARY_DIR}/init.tcl COPYONLY)
configure_file(scripts/bindkeys.tcl ${CMAKE_CURRENT_BINARY_DIR}/bindkeys.tcl COPYONLY)
configure_file(scripts/transcript_filters.tcl ${CMAKE_CURRENT_BINARY_DIR}/transcript_filters.tcl COPYONLY)
