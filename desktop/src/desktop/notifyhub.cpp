#include "desktop/notifyhub.hpp"

#include <QApplication>
#include <QHash>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

struct Watch
{
    QPointer<QObject> owner;
    std::function<void()> fn;
};

QHash<int, QString> gSerialToApp;
QHash<uint, QString> gIdToApp;
QHash<QString, int> gCounts;
QByteArray gBuffer;
int gExpect = 0;
int gPendingSerial = -1;
int gReplySerial = -1;
std::vector<Watch> gWatches;

void notify()
{
    std::vector<Watch> alive;
    alive.reserve(gWatches.size());

    for (Watch &watch : gWatches) {
        if (watch.owner == nullptr) {
            continue;
        }

        watch.fn();
        alive.push_back(std::move(watch));
    }

    gWatches.swap(alive);
}

QString normalize(QString name)
{
    return name.trimmed().toLower();
}

int captureNumber(const QString &line, const QString &key)
{
    const QRegularExpression expression(key + QStringLiteral("=(\\d+)"));
    const QRegularExpressionMatch match = expression.match(line);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QString captureString(const QString &line)
{
    const int start = line.indexOf(QLatin1Char('"'));
    const int end = line.lastIndexOf(QLatin1Char('"'));

    if (start < 0 || end <= start) {
        return {};
    }

    return line.mid(start + 1, end - start - 1);
}

bool matches(const QString &key, const QString &label, const QString &iconName)
{
    if (key.size() < 3) {
        return false;
    }

    const QString name = label.trimmed().toLower();
    const QString icon = iconName.trimmed().toLower();
    const bool labelMatch = name.size() >= 3 && (key.contains(name) || name.contains(key));
    const bool iconMatch = icon.size() >= 3 && (key.contains(icon) || icon.contains(key));
    return labelMatch || iconMatch;
}

void handleLine(const QString &line)
{
    if (line.contains(QStringLiteral("member=Notify"))) {
        gPendingSerial = captureNumber(line, QStringLiteral("serial"));
        gExpect = 1;
        return;
    }

    if (gExpect == 1 && line.contains(QStringLiteral("string"))) {
        const QString app = normalize(captureString(line));

        if (gPendingSerial >= 0 && !app.isEmpty()) {
            gSerialToApp.insert(gPendingSerial, app);
        }

        gExpect = 0;
        return;
    }

    if (line.contains(QStringLiteral("reply_serial="))) {
        gReplySerial = captureNumber(line, QStringLiteral("reply_serial"));
        gExpect = 2;
        return;
    }

    if ((gExpect == 2 || gExpect == 3) && line.contains(QStringLiteral("uint32"))) {
        const uint id = line.simplified().section(QLatin1Char(' '), -1).toUInt();

        if (gExpect == 2) {
            const QString app = gSerialToApp.take(gReplySerial);

            if (!app.isEmpty()) {
                gIdToApp.insert(id, app);
                gCounts[app] += 1;
                notify();
            }
        } else {
            const QString app = gIdToApp.take(id);

            if (!app.isEmpty() && gCounts.contains(app)) {
                gCounts[app] = std::max(0, gCounts.value(app) - 1);

                if (gCounts.value(app) == 0) {
                    gCounts.remove(app);
                }

                notify();
            }
        }

        gExpect = 0;
        return;
    }

    if (line.contains(QStringLiteral("member=NotificationClosed"))) {
        gExpect = 3;
    }
}

void consume(QProcess *process)
{
    gBuffer += process->readAllStandardOutput();

    while (true) {
        const int split = gBuffer.indexOf('\n');

        if (split < 0) {
            break;
        }

        const QString line = QString::fromUtf8(gBuffer.left(split));
        gBuffer.remove(0, split + 1);
        handleLine(line);
    }
}

}  // namespace

namespace EdenNotify {

void start()
{
    if (qApp == nullptr) {
        return;
    }

    auto *process = new QProcess(qApp);
    QObject::connect(process, &QProcess::readyReadStandardOutput, qApp, [process]() {
        consume(process);
    });
    process->start(
        QStringLiteral("dbus-monitor"),
        {QStringLiteral("--session"), QStringLiteral("interface='org.freedesktop.Notifications'")});
}

int countFor(const QString &label, const QString &iconName)
{
    int total = 0;

    for (auto it = gCounts.cbegin(); it != gCounts.cend(); ++it) {
        if (matches(it.key(), label, iconName)) {
            total += it.value();
        }
    }

    return total;
}

void clear(const QString &label, const QString &iconName)
{
    QList<QString> keys;

    for (auto it = gCounts.cbegin(); it != gCounts.cend(); ++it) {
        if (matches(it.key(), label, iconName)) {
            keys.push_back(it.key());
        }
    }

    if (keys.isEmpty()) {
        return;
    }

    for (const QString &key : keys) {
        gCounts.remove(key);
    }

    notify();
}

void watch(QObject *owner, std::function<void()> fn)
{
    if (owner == nullptr || !fn) {
        return;
    }

    gWatches.push_back(Watch{owner, std::move(fn)});
}

}  // namespace EdenNotify
