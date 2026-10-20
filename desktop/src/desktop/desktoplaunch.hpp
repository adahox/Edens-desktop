#pragma once

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace EdenDesktop {

struct ClientWindow
{
    quint32 window = 0;
    qint64 pid = 0;
    QString instance;
    QString className;
    QString title;
    bool hidden = false;
};

[[nodiscard]] QHash<QString, QString> fields(const QString &desktopPath);
[[nodiscard]] QString localizedName(const QHash<QString, QString> &desktopFields);
[[nodiscard]] QIcon iconFor(const QHash<QString, QString> &desktopFields);
[[nodiscard]] QStringList splitExec(const QString &command);
[[nodiscard]] QStringList matchHints(const QHash<QString, QString> &desktopFields);
bool launchDesktopFile(const QString &desktopPath, qint64 *pid = nullptr);
bool launchCommand(const QStringList &arguments, bool terminal, qint64 *pid = nullptr);

[[nodiscard]] QVector<ClientWindow> listClientWindows();
[[nodiscard]] QVector<ClientWindow> matchingWindows(
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints = {});
[[nodiscard]] QPixmap captureWindowPixmap(quint32 window);
bool activateWindow(quint32 window);
bool closeWindow(quint32 window);
bool activateMatching(
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints = {});
bool closeMatching(
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints = {});

}  // namespace EdenDesktop
