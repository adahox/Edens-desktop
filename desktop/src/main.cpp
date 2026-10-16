
#include <QApplication>
#include <QScreen>
#include <QWidget>
#include "desktop/desktopwindow.hpp"
#include <QPainter>
#include <QColor>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QGuiApplication>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <QProcess>
#include <QFileInfo>
#include "filemanager/filemanagerwindow.hpp"

static bool setDesktopWindowType(xcb_window_t window)
{
    auto *native =
        qGuiApp->nativeInterface<
            QNativeInterface::QX11Application
        >();

    if (native == nullptr) {
        return false;
    }

    xcb_connection_t *connection = native->connection();

    if (connection == nullptr ||
        xcb_connection_has_error(connection)) {
        return false;
    }

    const char *propertyName = "_NET_WM_WINDOW_TYPE";
    const char *typeName = "_NET_WM_WINDOW_TYPE_DESKTOP";

    auto propertyCookie = xcb_intern_atom(
        connection,
        0,
        static_cast<uint16_t>(std::strlen(propertyName)),
        propertyName
    );

    auto typeCookie = xcb_intern_atom(
        connection,
        0,
        static_cast<uint16_t>(std::strlen(typeName)),
        typeName
    );

    auto *propertyReply = xcb_intern_atom_reply(
        connection,
        propertyCookie,
        nullptr
    );

    auto *typeReply = xcb_intern_atom_reply(
        connection,
        typeCookie,
        nullptr
    );

    if (propertyReply == nullptr || typeReply == nullptr) {
        std::free(propertyReply);
        std::free(typeReply);
        return false;
    }

    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE,
        window,
        propertyReply->atom,
        XCB_ATOM_ATOM,
        32,
        1,
        &typeReply->atom
    );

    const uint32_t stackMode[] = {XCB_STACK_MODE_BELOW};

    xcb_configure_window(
        connection,
        window,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stackMode
    );

    xcb_flush(connection);

    std::free(propertyReply);
    std::free(typeReply);

    return !xcb_connection_has_error(connection);
}


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DesktopWindow desktop;

    QScreen *screen = QApplication::primaryScreen();

    if (screen == nullptr) {
        return 1;
    }

    desktop.setGeometry(screen->geometry());
    desktop.show();

    if (!setDesktopWindowType(static_cast<xcb_window_t>(desktop.winId()))) {
        return 1;
    }

    return app.exec();
}

