#include <QApplication>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include "desktop/desktopwindow.hpp"
#include "desktop/dockbar.hpp"
#include "desktop/monitorlayout.hpp"
#include "desktop/programsearch.hpp"
#include "desktop/screenshot.hpp"
#include "desktop/shelllayout.hpp"
#include "desktop/notifyhub.hpp"
#include "desktop/snowtheme.hpp"
#include "desktop/statusbar.hpp"
#include "desktop/systemmanager.hpp"
#include <QTimer>
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
#include <QStandardPaths>
#include "filemanager/filemanagerwindow.hpp"

namespace {

bool processNamedRunning(const QString &needle)
{
    QProcess scanner;
    scanner.start(
        QStringLiteral("pgrep"),
        {QStringLiteral("-f"), needle});
    scanner.waitForFinished(800);
    return scanner.exitCode() == 0 &&
           !scanner.readAllStandardOutput().trimmed().isEmpty();
}

void ensurePolkitAgent()
{
    const QStringList agents = {
        QStringLiteral("/usr/lib/policykit-1-gnome/polkit-gnome-authentication-agent-1"),
        QStringLiteral("/usr/libexec/polkit-gnome-authentication-agent-1"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec/polkit-kde-authentication-agent-1"),
        QStandardPaths::findExecutable(QStringLiteral("lxqt-policykit-agent")),
        QStandardPaths::findExecutable(QStringLiteral("mate-polkit")),
    };

    for (const QString &agent : agents) {
        if (agent.isEmpty() || !QFileInfo::exists(agent)) {
            continue;
        }

        if (processNamedRunning(agent)) {
            return;
        }
    }

    for (const QString &agent : agents) {
        if (agent.isEmpty() || !QFileInfo::exists(agent)) {
            continue;
        }

        if (QProcess::startDetached(agent, {})) {
            return;
        }
    }
}

}  // namespace

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

    EdenSnow::apply(&app);
    EdenNotify::start();

    EdenMonitors::applySaved();
    ensurePolkitAgent();

    DesktopWindow desktop;

    QScreen *screen = QApplication::primaryScreen();

    if (screen == nullptr) {
        return 1;
    }

    desktop.setAttribute(Qt::WA_NativeWindow);
    EdenShell::bind(&desktop, nullptr, nullptr);
    EdenShell::relayout();

    if (!setDesktopWindowType(static_cast<xcb_window_t>(desktop.winId()))) {
        return 1;
    }

    desktop.show();

    DockBar dock;
    DockBar::bind(&dock);

    StatusBar statusBar;

    EdenShell::bind(&desktop, &dock, &statusBar);
    EdenShell::relayout();
    dock.show();
    statusBar.show();

    EdenShell::watchScreens(&app);
    QTimer::singleShot(200, &app, []() { EdenShell::relayout(); });
    QTimer::singleShot(600, &app, []() { EdenShell::relayout(); });

    ProgramSearch::install();
    SystemManager::install();
    ScreenshotService::install();

    return app.exec();
}
