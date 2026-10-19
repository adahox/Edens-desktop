
#include <QApplication>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include "desktop/desktopwindow.hpp"
#include "desktop/dockbar.hpp"
#include "desktop/programsearch.hpp"
#include "desktop/statusbar.hpp"
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
#include <QRect>
#include <QtGlobal>
#include <QProcess>
#include <QFileInfo>
#include "filemanager/filemanagerwindow.hpp"

static QRect workAreaOr(const QRect &fallback)
{
    auto *native =
        qGuiApp->nativeInterface<
            QNativeInterface::QX11Application
        >();

    if (native == nullptr || native->connection() == nullptr) {
        return fallback;
    }

    xcb_connection_t *connection = native->connection();
    const xcb_setup_t *setup = xcb_get_setup(connection);

    if (setup == nullptr) {
        return fallback;
    }

    const xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;

    if (screen == nullptr) {
        return fallback;
    }

    const char *name = "_NET_WORKAREA";
    auto atomCookie = xcb_intern_atom(
        connection,
        1,
        static_cast<uint16_t>(std::strlen(name)),
        name);
    auto *atomReply = xcb_intern_atom_reply(connection, atomCookie, nullptr);

    if (atomReply == nullptr || atomReply->atom == XCB_ATOM_NONE) {
        std::free(atomReply);
        return fallback;
    }

    auto propertyCookie = xcb_get_property(
        connection,
        0,
        screen->root,
        atomReply->atom,
        XCB_ATOM_CARDINAL,
        0,
        4);
    std::free(atomReply);

    auto *property = xcb_get_property_reply(connection, propertyCookie, nullptr);

    if (property == nullptr || xcb_get_property_value_length(property) < 16) {
        std::free(property);
        return fallback;
    }

    const auto *values = static_cast<const uint32_t *>(
        xcb_get_property_value(property));
    const QRect area(
        static_cast<int>(values[0]),
        static_cast<int>(values[1]),
        static_cast<int>(values[2]),
        static_cast<int>(values[3]));
    std::free(property);

    if (area.width() < 100 || area.height() < 100) {
        return fallback;
    }

    return area;
}

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
    qunsetenv("QT_QPA_PLATFORMTHEME");
    qputenv("QT_STYLE_OVERRIDE", "Fusion");

    QApplication app(argc, argv);

    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

    app.setStyleSheet(QStringLiteral(R"(
        QMenu {
            background: #1E2430;
            color: #F2F4F8;
            border: 1px solid #3A4254;
            padding: 4px;
        }

        QMenu::item {
            padding: 6px 28px;
            background: transparent;
        }

        QMenu::item:selected {
            background: #2F6F4E;
        }

        QMenu::separator {
            height: 1px;
            background: #3A4254;
            margin: 4px 8px;
        }
    )"));

    DesktopWindow desktop;

    QScreen *screen = QApplication::primaryScreen();

    if (screen == nullptr) {
        return 1;
    }

    const QRect full = screen->geometry();
    QRect area = workAreaOr(full);

    if (area.top() < StatusBar::kBarHeight) {
        area.setTop(StatusBar::kBarHeight);
    }

    desktop.setAttribute(Qt::WA_NativeWindow);
    desktop.setGeometry(area);

    if (!setDesktopWindowType(static_cast<xcb_window_t>(desktop.winId()))) {
        return 1;
    }

    desktop.show();

    DockBar dock;
    dock.place(full);
    dock.show();

    StatusBar statusBar;
    statusBar.place(full);
    statusBar.show();

    ProgramSearch::install();

    return app.exec();
}

