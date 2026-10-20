#include "desktop/monitorlayout.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

namespace {

QString edenConfigDir()
{
    const QString path =
        QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(path);
    return path;
}

QString preferredModeFor(const QString &output)
{
    QProcess process;
    process.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});

    if (!process.waitForFinished(4000)) {
        return {};
    }

    const QString outputText = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = outputText.split(QLatin1Char('\n'));
    bool inOutput = false;

    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1Char(' ')) && !line.startsWith(QLatin1Char('\t'))) {
            inOutput = line.startsWith(output + QLatin1Char(' ')) &&
                       line.contains(QLatin1String(" connected"));
            continue;
        }

        if (!inOutput) {
            continue;
        }

        const QRegularExpression modeRegex(QStringLiteral("^\\s*(\\d+x\\d+)"));
        const QRegularExpressionMatch match = modeRegex.match(line);

        if (match.hasMatch()) {
            return match.captured(1);
        }
    }

    return {};
}

bool outputConnected(const QString &output)
{
    QProcess process;
    process.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});

    if (!process.waitForFinished(4000)) {
        return false;
    }

    const QString needle = output + QLatin1String(" connected");
    return QString::fromUtf8(process.readAllStandardOutput()).contains(needle);
}

bool writeApplyScript(const QStringList &arguments)
{
    const QString path = EdenMonitors::applyScriptPath();
    QSaveFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "#!/bin/bash\n";
    stream << "set -euo pipefail\n";
    stream << "command -v xrandr >/dev/null 2>&1 || exit 0\n";
    stream << "xrandr";

    for (const QString &argument : arguments) {
        QString escaped = argument;
        escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
        stream << " '" << escaped << "'";
    }

    stream << '\n';

    if (!file.commit()) {
        return false;
    }

    QFile::setPermissions(
        path,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther);
    return true;
}

}  // namespace

namespace EdenMonitors {

QString configPath()
{
    return QDir(edenConfigDir()).filePath(QStringLiteral("monitors.conf"));
}

QString applyScriptPath()
{
    return QDir(edenConfigDir()).filePath(QStringLiteral("apply-monitors.sh"));
}

bool save(const Layout &layout)
{
    if (layout.primary.isEmpty()) {
        return false;
    }

    QSaveFile file(configPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "primary=" << layout.primary << '\n';
    stream << "primaryMode=" << layout.primaryMode << '\n';
    stream << "secondary=" << layout.secondary << '\n';
    stream << "secondaryMode=" << layout.secondaryMode << '\n';
    stream << "side=" << layout.side << '\n';

    if (!file.commit()) {
        return false;
    }

    Layout resolved = layout;

    if (resolved.primaryMode.isEmpty()) {
        resolved.primaryMode = preferredModeFor(resolved.primary);
    }

    if (!resolved.secondary.isEmpty() && resolved.secondaryMode.isEmpty()) {
        resolved.secondaryMode = preferredModeFor(resolved.secondary);
    }

    QStringList arguments;
    arguments << QStringLiteral("--output") << resolved.primary
              << QStringLiteral("--primary")
              << QStringLiteral("--mode") << resolved.primaryMode
              << QStringLiteral("--pos") << QStringLiteral("0x0");

    if (!resolved.secondary.isEmpty() &&
        resolved.secondary != resolved.primary &&
        !resolved.secondaryMode.isEmpty()) {
        arguments << QStringLiteral("--output") << resolved.secondary
                  << QStringLiteral("--mode") << resolved.secondaryMode;

        if (resolved.side == QLatin1String("left")) {
            arguments << QStringLiteral("--left-of") << resolved.primary;
        } else if (resolved.side == QLatin1String("above")) {
            arguments << QStringLiteral("--above") << resolved.primary;
        } else if (resolved.side == QLatin1String("below")) {
            arguments << QStringLiteral("--below") << resolved.primary;
        } else {
            arguments << QStringLiteral("--right-of") << resolved.primary;
        }
    }

    writeApplyScript(arguments);
    return true;
}

bool load(Layout *layout)
{
    if (layout == nullptr) {
        return false;
    }

    QFile file(configPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    Layout loaded;

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

        if (key == QLatin1String("primary")) {
            loaded.primary = value;
        } else if (key == QLatin1String("primaryMode")) {
            loaded.primaryMode = value;
        } else if (key == QLatin1String("secondary")) {
            loaded.secondary = value;
        } else if (key == QLatin1String("secondaryMode")) {
            loaded.secondaryMode = value;
        } else if (key == QLatin1String("side")) {
            loaded.side = value;
        }
    }

    if (loaded.primary.isEmpty()) {
        return false;
    }

    if (loaded.side.isEmpty()) {
        loaded.side = QStringLiteral("right");
    }

    *layout = loaded;
    return true;
}

bool apply(const Layout &layout)
{
    if (layout.primary.isEmpty() || !outputConnected(layout.primary)) {
        return false;
    }

    QString primaryMode = layout.primaryMode;
    QString secondaryMode = layout.secondaryMode;

    if (primaryMode.isEmpty()) {
        primaryMode = preferredModeFor(layout.primary);
    }

    if (primaryMode.isEmpty()) {
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--output") << layout.primary
              << QStringLiteral("--primary")
              << QStringLiteral("--mode") << primaryMode
              << QStringLiteral("--pos") << QStringLiteral("0x0");

    const bool hasSecondary =
        !layout.secondary.isEmpty() &&
        layout.secondary != layout.primary &&
        outputConnected(layout.secondary);

    if (hasSecondary) {
        if (secondaryMode.isEmpty()) {
            secondaryMode = preferredModeFor(layout.secondary);
        }

        if (!secondaryMode.isEmpty()) {
            arguments << QStringLiteral("--output") << layout.secondary
                      << QStringLiteral("--mode") << secondaryMode;

            if (layout.side == QLatin1String("left")) {
                arguments << QStringLiteral("--left-of") << layout.primary;
            } else if (layout.side == QLatin1String("above")) {
                arguments << QStringLiteral("--above") << layout.primary;
            } else if (layout.side == QLatin1String("below")) {
                arguments << QStringLiteral("--below") << layout.primary;
            } else {
                arguments << QStringLiteral("--right-of") << layout.primary;
            }
        }
    }

    QProcess query;
    query.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});

    if (query.waitForFinished(4000)) {
        const QRegularExpression lineRegex(
            QStringLiteral("^(\\S+)\\s+connected"));
        const QString text = QString::fromUtf8(query.readAllStandardOutput());

        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch match = lineRegex.match(line);

            if (!match.hasMatch()) {
                continue;
            }

            const QString name = match.captured(1);

            if (name == layout.primary ||
                (hasSecondary && name == layout.secondary)) {
                continue;
            }

            arguments << QStringLiteral("--output") << name
                      << QStringLiteral("--off");
        }
    }

    QProcess process;
    process.start(QStringLiteral("xrandr"), arguments);

    if (!process.waitForFinished(8000) || process.exitCode() != 0) {
        return false;
    }

    Layout persisted = layout;
    persisted.primaryMode = primaryMode;

    if (hasSecondary) {
        persisted.secondaryMode = secondaryMode;
    } else {
        persisted.secondary.clear();
        persisted.secondaryMode.clear();
    }

    save(persisted);
    return true;
}

bool applySaved()
{
    Layout layout;

    if (!load(&layout)) {
        return false;
    }

    return apply(layout);
}

}  // namespace EdenMonitors
