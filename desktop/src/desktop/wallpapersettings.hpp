#pragma once

#include <QString>
#include <QStringList>

namespace EdenWallpaper {

[[nodiscard]] QString configPath();
[[nodiscard]] QString defaultPath();
[[nodiscard]] QString currentPath();
[[nodiscard]] QStringList bundledPaths();
bool save(const QString &path);
bool apply(const QString &path);
void refreshDesktop();

}  // namespace EdenWallpaper
