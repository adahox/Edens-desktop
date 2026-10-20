#include "desktop/shelllayout.hpp"

#include "desktop/desktopwindow.hpp"
#include "desktop/dockbar.hpp"
#include "desktop/statusbar.hpp"

#include <QGuiApplication>
#include <QObject>
#include <QRect>
#include <QScreen>
#include <QTimer>

namespace {

DesktopWindow *gDesktop = nullptr;
DockBar *gDock = nullptr;
StatusBar *gStatus = nullptr;

QRect virtualDesktopGeometry()
{
    QRect geometry;

    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen == nullptr) {
            continue;
        }

        geometry = geometry.united(screen->geometry());
    }

    if (!geometry.isEmpty()) {
        return geometry;
    }

    QScreen *primary = QGuiApplication::primaryScreen();
    return primary == nullptr ? QRect() : primary->virtualGeometry();
}

void connectScreen(QScreen *screen, QObject *context)
{
    if (screen == nullptr || context == nullptr) {
        return;
    }

    QObject::connect(
        screen,
        &QScreen::geometryChanged,
        context,
        [](const QRect &) { EdenShell::relayout(); });
    QObject::connect(
        screen,
        &QScreen::availableGeometryChanged,
        context,
        [](const QRect &) { EdenShell::relayout(); });
}

}  // namespace

namespace EdenShell {

void bind(DesktopWindow *desktop, DockBar *dock, StatusBar *status)
{
    gDesktop = desktop;
    gDock = dock;
    gStatus = status;
}

void relayout()
{
    QScreen *primary = QGuiApplication::primaryScreen();

    if (primary == nullptr) {
        return;
    }

    const QRect primaryGeometry = primary->geometry();
    const QRect virtualGeometry = virtualDesktopGeometry();

    if (gStatus != nullptr) {
        gStatus->place(primaryGeometry);
    }

    if (gDock != nullptr) {
        gDock->place(primaryGeometry);
    }

    if (gDesktop == nullptr || virtualGeometry.isEmpty()) {
        return;
    }

    // Full virtual desktop for wallpaper (all monitors, full height).
    // Status bar and dock float above; only app windows use the work area.
    if (gDesktop->geometry() != virtualGeometry) {
        gDesktop->setGeometry(virtualGeometry);
    }

    gDesktop->refreshWallpaper();
}

void watchScreens(QObject *context)
{
    if (context == nullptr) {
        return;
    }

    QObject::connect(
        qGuiApp,
        &QGuiApplication::screenAdded,
        context,
        [context](QScreen *screen) {
            connectScreen(screen, context);
            EdenShell::relayout();
        });
    QObject::connect(
        qGuiApp,
        &QGuiApplication::screenRemoved,
        context,
        [](QScreen *) { EdenShell::relayout(); });
    QObject::connect(
        qGuiApp,
        &QGuiApplication::primaryScreenChanged,
        context,
        [context](QScreen *screen) {
            connectScreen(screen, context);
            EdenShell::relayout();
        });

    for (QScreen *screen : QGuiApplication::screens()) {
        connectScreen(screen, context);
    }

    QTimer::singleShot(0, context, []() { EdenShell::relayout(); });
}

}  // namespace EdenShell
