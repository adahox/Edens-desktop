#pragma once

class DesktopWindow;
class DockBar;
class QObject;
class StatusBar;

namespace EdenShell {

void bind(DesktopWindow *desktop, DockBar *dock, StatusBar *status);
void relayout();
void refreshWallpaper();
void watchScreens(QObject *context);

}  // namespace EdenShell
