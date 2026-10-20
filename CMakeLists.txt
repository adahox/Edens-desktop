
cmake_minimum_required(VERSION 3.20)

project(Eden LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XRANDR REQUIRED IMPORTED_TARGET xrandr)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

qt_standard_project_setup()

# Eden Desktop: C++ / Qt 6
qt_add_executable(eden-desktop
    desktop/src/main.cpp
    desktop/src/desktop/desktopwindow.hpp
    desktop/src/desktop/desktopwindow.cpp
    desktop/src/desktop/dockbar.hpp
    desktop/src/desktop/dockbar.cpp
    desktop/src/desktop/statusbar.hpp
    desktop/src/desktop/statusbar.cpp
    desktop/src/desktop/programsearch.hpp
    desktop/src/desktop/programsearch.cpp
    desktop/src/desktop/systemmanager.hpp
    desktop/src/desktop/systemmanager.cpp
    desktop/src/filemanager/filemanagerwindow.hpp
    desktop/src/filemanager/filemanagerwindow.cpp
    desktop/src/windowmanager/windowmanager.hpp
    desktop/src/windowmanager/windowmanager.cpp
    desktop/src/windowmanager/windowmanagerprocessmessages.cpp
    desktop/src/windowmanager/windowmanagerdesktop.cpp
)

target_include_directories(eden-desktop
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src
)

target_link_libraries(eden-desktop
    PRIVATE
        Qt6::Widgets
        PkgConfig::XCB
        PkgConfig::X11
        PkgConfig::XRANDR
        PkgConfig::CAIRO
        PkgConfig::GLIB
        /usr/lib/x86_64-linux-gnu/librsvg-2.so.2
        jpeg
)

# Eden .deb installer (Apple-style drag to install)
qt_add_executable(eden-installer
    desktop/src/installer/main.cpp
    desktop/src/installer/debinstaller.hpp
    desktop/src/installer/debinstaller.cpp
)

target_include_directories(eden-installer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src
)

target_link_libraries(eden-installer
    PRIVATE
        Qt6::Widgets
)

# Eden File Manager: C / GTK 3
add_executable(edens
    filemanager/src/main.c
    filemanager/src/processors/directories.c
    filemanager/src/ui/window/window.c
    filemanager/src/ui/mainbox.c
    filemanager/src/ui/toolbar.c
    filemanager/src/ui/filearea.c
    filemanager/src/ui/scrolledwindow.c
    filemanager/src/ui/fileitem.c
    filemanager/src/ui/window/menu.c
    filemanager/src/ui/window/menu_actions.c
    filemanager/src/ui/actionBar.c
    
)

target_include_directories(edens
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/filemanager/src
)

target_compile_options(edens
    PRIVATE
        -Wall
        -Wextra
)

target_link_libraries(edens
    PRIVATE
        PkgConfig::GTK3
)

add_executable(windowmanager-test
    desktop/tests/windowmanager_test.cpp
    desktop/src/windowmanager/windowmanager.cpp
    desktop/src/windowmanager/windowmanagerprocessmessages.cpp
    desktop/src/windowmanager/windowmanagerdesktop.cpp
)

add_executable(windowmanager-policy-test
    desktop/tests/windowmanager_policy_test.cpp
)

target_include_directories(windowmanager-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src
)

target_link_libraries(windowmanager-test
    PRIVATE
        PkgConfig::XCB
        PkgConfig::X11
        PkgConfig::XRANDR
        PkgConfig::CAIRO
        jpeg
)

target_include_directories(windowmanager-policy-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src
)

target_link_libraries(windowmanager-policy-test
    PRIVATE
        PkgConfig::XCB
)

enable_testing()
add_test(NAME windowmanager-policy COMMAND windowmanager-policy-test)