#include "desktop/wallpapersettings.hpp"

#include "desktop/dockbar.hpp"
#include "desktop/shelllayout.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace {

QString edenConfigDir()
{
    const QString path = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(path);
    return path;
}

QString findBundledDefault()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath(QStringLiteral("../assets/wallpapers/default.jpg")),
        binaryDirectory.filePath(QStringLiteral("assets/wallpapers/default.jpg")),
        QStringLiteral("assets/wallpapers/default.jpg"),
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }

    return {};
}

QString wallpapersDirectory()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath(QStringLiteral("../assets/wallpapers")),
        binaryDirectory.filePath(QStringLiteral("assets/wallpapers")),
        QStringLiteral("assets/wallpapers"),
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QDir(cleaned).exists()) {
            return cleaned;
        }
    }

    return {};
}

}  // namespace

namespace EdenWallpaper {

QString configPath()
{
    return QDir(edenConfigDir()).filePath(QStringLiteral("wallpaper.conf"));
}

QString defaultPath()
{
    return findBundledDefault();
}

QString currentPath()
{
    QFile file(configPath());

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();

            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }

            const int split = line.indexOf(QLatin1Char('='));

            if (split <= 0) {
                continue;
            }

            const QString key = line.left(split).trimmed();
            const QString value = line.mid(split + 1).trimmed();

            if (key == QLatin1String("path") && QFileInfo::exists(value)) {
                return value;
            }
        }
    }

    return defaultPath();
}

QStringList bundledPaths()
{
    QStringList paths;
    const QString directory = wallpapersDirectory();

    if (directory.isEmpty()) {
        const QString fallback = defaultPath();

        if (!fallback.isEmpty()) {
            paths.push_back(fallback);
        }

        return paths;
    }

    const QStringList names = QDir(directory).entryList(
        {QStringLiteral("*.jpg"),
         QStringLiteral("*.jpeg"),
         QStringLiteral("*.png"),
         QStringLiteral("*.webp"),
         QStringLiteral("*.bmp")},
        QDir::Files,
        QDir::Name);

    for (const QString &name : names) {
        paths.push_back(QDir(directory).filePath(name));
    }

    return paths;
}

bool save(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();

    if (!QFileInfo::exists(absolute)) {
        return false;
    }

    QSaveFile file(configPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "path=" << absolute << '\n';
    return file.commit();
}

void refreshDesktop()
{
    EdenShell::refreshWallpaper();

    if (DockBar *dock = DockBar::instance()) {
        dock->applySettings(dock->settings());
    }
}

bool apply(const QString &path)
{
    if (!save(path)) {
        return false;
    }

    refreshDesktop();
    return true;
}

}  // namespace EdenWallpaper
