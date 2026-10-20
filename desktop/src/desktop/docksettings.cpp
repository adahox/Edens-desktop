#include "desktop/docksettings.hpp"
#include "desktop/dockbar.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace {

QString edenConfigDir()
{
    const QString path = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(path);
    return path;
}

int clampInt(int value, int minimum, int maximum)
{
    return std::clamp(value, minimum, maximum);
}

}  // namespace

namespace EdenDock {

void Settings::clamp()
{
    iconSize = clampInt(iconSize, 32, 80);
    iconHoverSize = clampInt(iconHoverSize, std::max(iconSize + 4, 40), 112);
    fontSize = clampInt(fontSize, 9, 20);
    bottomMargin = clampInt(bottomMargin, 0, 40);
    iconGap = clampInt(iconGap, 2, 20);
}

QString configPath()
{
    return QDir(edenConfigDir()).filePath(QStringLiteral("dock.conf"));
}

Settings defaults()
{
    return {};
}

Settings load()
{
    Settings settings = defaults();
    QFile file(configPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return settings;
    }

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
        bool ok = false;
        const int number = value.toInt(&ok);

        if (!ok) {
            continue;
        }

        if (key == QLatin1String("iconSize")) {
            settings.iconSize = number;
        } else if (key == QLatin1String("iconHoverSize")) {
            settings.iconHoverSize = number;
        } else if (key == QLatin1String("fontSize")) {
            settings.fontSize = number;
        } else if (key == QLatin1String("bottomMargin")) {
            settings.bottomMargin = number;
        } else if (key == QLatin1String("iconGap")) {
            settings.iconGap = number;
        }
    }

    settings.clamp();
    return settings;
}

bool save(const Settings &settings)
{
    Settings clamped = settings;
    clamped.clamp();

    QSaveFile file(configPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "iconSize=" << clamped.iconSize << '\n';
    stream << "iconHoverSize=" << clamped.iconHoverSize << '\n';
    stream << "fontSize=" << clamped.fontSize << '\n';
    stream << "bottomMargin=" << clamped.bottomMargin << '\n';
    stream << "iconGap=" << clamped.iconGap << '\n';

    return file.commit();
}

void apply(const Settings &settings)
{
    Settings clamped = settings;
    clamped.clamp();

    if (DockBar *dock = DockBar::instance()) {
        dock->applySettings(clamped);
    }
}

}  // namespace EdenDock
