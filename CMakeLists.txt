
cmake_minimum_required(VERSION 3.20)

project(Eden LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus Network)
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
    desktop/src/desktop/desktoplaunch.hpp
    desktop/src/desktop/desktoplaunch.cpp
    desktop/src/desktop/statusbar.hpp
    desktop/src/desktop/statusbar.cpp
    desktop/src/desktop/networkmenu.hpp
    desktop/src/desktop/networkmenu.cpp
    desktop/src/desktop/shelllayout.hpp
    desktop/src/desktop/shelllayout.cpp
    desktop/src/desktop/monitorlayout.hpp
    desktop/src/desktop/monitorlayout.cpp
    desktop/src/desktop/docksettings.hpp
    desktop/src/desktop/docksettings.cpp
    desktop/src/desktop/wallpapersettings.hpp
    desktop/src/desktop/wallpapersettings.cpp
    desktop/src/desktop/screenshot.hpp
    desktop/src/desktop/screenshot.cpp
    desktop/src/desktop/programsearch.hpp
    desktop/src/desktop/programsearch.cpp
    desktop/src/desktop/systemmanager.hpp
    desktop/src/desktop/systemmanager.cpp
    desktop/src/desktop/settingswindow.hpp
    desktop/src/desktop/settingswindow.cpp
    desktop/src/desktop/glassstyle.hpp
    desktop/src/desktop/glassstyle.cpp
    desktop/src/desktop/sysmetrics.hpp
    desktop/src/desktop/sysmetrics.cpp
    desktop/src/desktop/notifyhub.hpp
    desktop/src/desktop/notifyhub.cpp
    desktop/src/desktop/desktopwidgets.hpp
    desktop/src/desktop/desktopwidgets.cpp
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
        Qt6::DBus
        Qt6::Network
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

# Eden File Manager: C / GTK 3 (vendored from ~/dev/edens)
find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

set(EDENS_GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/filemanager/gresources.xml)
set(EDENS_RESOURCES_C ${CMAKE_CURRENT_BINARY_DIR}/edens_resources.c)

add_custom_command(
    OUTPUT ${EDENS_RESOURCES_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/filemanager
        --generate-source
        --target=${EDENS_RESOURCES_C}
        ${EDENS_GRESOURCE_XML}
    DEPENDS
        ${EDENS_GRESOURCE_XML}
        ${CMAKE_CURRENT_SOURCE_DIR}/filemanager/data/edens.css
        ${CMAKE_CURRENT_SOURCE_DIR}/filemanager/data/icons/icon-task-manager.png
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/filemanager
    VERBATIM
)

add_executable(edens
    filemanager/src/main.c
    filemanager/src/core/widget/events.c
    filemanager/src/core/widgetFactory.c
    filemanager/src/processors/directories.c
    filemanager/src/ui/window/window.c
    filemanager/src/ui/header_bar/header_bar.c
    filemanager/src/ui/main_box/mainbox.c
    filemanager/src/ui/toolbar/toolbar.c
    filemanager/src/ui/navigation/navigation.c
    filemanager/src/ui/file_area/filearea.c
    filemanager/src/ui/scrolled_window/scrolledwindow.c
    filemanager/src/ui/fast_directories/fast_directories.c
    filemanager/src/ui/content/content.c
    filemanager/src/ui/explorer_pane/explorer_pane.c
    filemanager/src/ui/file_item/fileitem.c
    filemanager/src/ui/menu/menu.c
    filemanager/src/ui/menu_actions/menu_actions.c
    filemanager/src/ui/action_bar/actionBar.c
    filemanager/src/ui/theme/theme.c
    filemanager/src/ui/icons/icons.c
    filemanager/src/ui/app_icon/app_icon.c
    ${EDENS_RESOURCES_C}
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

add_custom_command(
    TARGET edens
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/filemanager/data
        $<TARGET_FILE_DIR:edens>/data
    VERBATIM
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