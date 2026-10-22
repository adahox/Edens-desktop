#include "desktop/sysmetrics.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>

#include <algorithm>

namespace {

QString unwrapText(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>()) {
        return unwrapText(value.value<QDBusVariant>().variant());
    }

    return value.toString();
}

QString readMediaTitle()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.isConnected()) {
        return {};
    }

    QDBusInterface dbus(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        bus);
    dbus.setTimeout(250);

    const QDBusReply<QStringList> names = dbus.call(QStringLiteral("ListNames"));

    if (!names.isValid()) {
        return {};
    }

    for (const QString &name : names.value()) {
        if (!name.startsWith(QStringLiteral("org.mpris.MediaPlayer2."))) {
            continue;
        }

        QDBusInterface props(
            name,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            bus);
        props.setTimeout(250);

        const QDBusReply<QVariant> playback = props.call(
            QStringLiteral("Get"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"),
            QStringLiteral("PlaybackStatus"));

        if (!playback.isValid() || unwrapText(playback.value()) != QLatin1String("Playing")) {
            continue;
        }

        const QDBusReply<QVariant> metadata = props.call(
            QStringLiteral("Get"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"),
            QStringLiteral("Metadata"));

        if (!metadata.isValid()) {
            continue;
        }

        QVariant raw = metadata.value();

        if (raw.canConvert<QDBusVariant>()) {
            raw = raw.value<QDBusVariant>().variant();
        }

        if (!raw.canConvert<QDBusArgument>()) {
            continue;
        }

        QDBusArgument argument = raw.value<QDBusArgument>();

        if (argument.currentType() != QDBusArgument::MapType) {
            continue;
        }

        QString title;
        argument.beginMap();

        while (!argument.atEnd()) {
            QString key;
            QVariant value;
            argument.beginMapEntry();
            argument >> key >> value;
            argument.endMapEntry();

            if (key == QLatin1String("xesam:title")) {
                title = unwrapText(value);
            }
        }

        argument.endMap();

        if (!title.isEmpty()) {
            return title;
        }
    }

    return {};
}

void readCpu(EdenMetrics::Snapshot &snap)
{
    static quint64 lastTotal = 0;
    static quint64 lastIdle = 0;

    QFile file(QStringLiteral("/proc/stat"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QList<QByteArray> parts = file.readLine().simplified().split(' ');

    if (parts.size() < 5 || parts.first() != "cpu") {
        return;
    }

    quint64 total = 0;

    for (int index = 1; index < parts.size(); ++index) {
        total += parts.at(index).toULongLong();
    }

    quint64 idle = parts.at(4).toULongLong();

    if (parts.size() > 5) {
        idle += parts.at(5).toULongLong();
    }

    if (lastTotal > 0 && total > lastTotal) {
        const quint64 deltaTotal = total - lastTotal;
        const quint64 deltaIdle = idle > lastIdle ? idle - lastIdle : 0;
        snap.cpuPercent = deltaTotal == 0
            ? 0
            : (1.0 - static_cast<double>(deltaIdle) / static_cast<double>(deltaTotal)) * 100.0;
        snap.cpuPercent = std::clamp(snap.cpuPercent, 0.0, 100.0);
        snap.cpuOk = true;
    }

    lastTotal = total;
    lastIdle = idle;
}

void readMemory(EdenMetrics::Snapshot &snap)
{
    QFile file(QStringLiteral("/proc/meminfo"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    quint64 total = 0;
    quint64 available = 0;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();

        if (line.startsWith("MemTotal:")) {
            total = line.simplified().split(' ').value(1).toULongLong();
        } else if (line.startsWith("MemAvailable:")) {
            available = line.simplified().split(' ').value(1).toULongLong();
        }
    }

    if (total == 0) {
        return;
    }

    snap.ramPercent = (1.0 - static_cast<double>(available) / static_cast<double>(total)) * 100.0;
    snap.ramPercent = std::clamp(snap.ramPercent, 0.0, 100.0);
    snap.ramOk = true;
}

void readUptime(EdenMetrics::Snapshot &snap)
{
    QFile file(QStringLiteral("/proc/uptime"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    snap.uptimeSeconds = file.readLine().simplified().split(' ').value(0).toDouble();
}

void considerTemp(int milliC, int &sum, int &count, int &maxC)
{
    if (milliC < 10000 || milliC > 125000) {
        return;
    }

    const int celsius = milliC / 1000;
    sum += celsius;
    ++count;
    maxC = std::max(maxC, celsius);
}

void readTemperatures(EdenMetrics::Snapshot &snap)
{
    int sum = 0;
    int count = 0;
    int maxC = 0;
    const QDir thermal(QStringLiteral("/sys/class/thermal"));

    for (const QString &zone : thermal.entryList(QStringList{QStringLiteral("thermal_zone*")}, QDir::Dirs)) {
        QFile file(thermal.filePath(zone + QStringLiteral("/temp")));

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        considerTemp(file.readAll().trimmed().toInt(), sum, count, maxC);
    }

    if (count == 0) {
        const QDir hwmon(QStringLiteral("/sys/class/hwmon"));

        for (const QString &card : hwmon.entryList(QStringList{QStringLiteral("hwmon*")}, QDir::Dirs)) {
            const QDir sensor(hwmon.filePath(card));

            for (const QString &input : sensor.entryList(QStringList{QStringLiteral("temp*_input")}, QDir::Files)) {
                QFile file(sensor.filePath(input));

                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    continue;
                }

                considerTemp(file.readAll().trimmed().toInt(), sum, count, maxC);
            }
        }
    }

    if (count == 0) {
        return;
    }

    snap.tempOk = true;
    snap.tempAvgC = sum / count;
    snap.tempMaxC = maxC;
}

void readNetwork(EdenMetrics::Snapshot &snap)
{
    static quint64 lastBytes = 0;
    static qint64 lastMs = 0;

    QFile file(QStringLiteral("/proc/net/dev"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    quint64 bytes = 0;
    file.readLine();
    file.readLine();

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const int colon = line.indexOf(':');

        if (colon < 0) {
            continue;
        }

        const QByteArray name = line.left(colon).trimmed();

        if (name == "lo") {
            continue;
        }

        const QList<QByteArray> parts = line.mid(colon + 1).simplified().split(' ');

        if (parts.size() < 9) {
            continue;
        }

        bytes += parts.at(0).toULongLong();
        bytes += parts.at(8).toULongLong();
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (lastMs > 0 && now > lastMs && bytes >= lastBytes) {
        snap.netBytesPerSec = static_cast<double>(bytes - lastBytes) * 1000.0 / static_cast<double>(now - lastMs);
    }

    lastBytes = bytes;
    lastMs = now;
}

void readBattery(EdenMetrics::Snapshot &snap)
{
    const QDir supply(QStringLiteral("/sys/class/power_supply"));

    for (const QString &entry : supply.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith(QLatin1String("BAT"))) {
            continue;
        }

        QFile capacity(supply.filePath(entry + QStringLiteral("/capacity")));

        if (!capacity.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        snap.batteryPercent = std::clamp(capacity.readAll().trimmed().toInt(), 0, 100);
        QFile status(supply.filePath(entry + QStringLiteral("/status")));

        if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray state = status.readAll().trimmed();
            snap.batteryCharging = state == "Charging" || state == "Full";
        }

        return;
    }
}

void readKeyboard(EdenMetrics::Snapshot &snap)
{
    static qint64 lastMs = 0;
    static QString cached = QStringLiteral("—");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (lastMs != 0 && now - lastMs < 60000) {
        snap.keyboard = cached;
        return;
    }

    lastMs = now;
    QProcess process;
    process.start(QStringLiteral("setxkbmap"), {QStringLiteral("-query")});

    if (process.waitForFinished(200) && process.exitCode() == 0) {
        const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'));

        for (const QString &line : lines) {
            if (!line.startsWith(QLatin1String("layout:"))) {
                continue;
            }

            cached = line.section(QLatin1Char(':'), 1).section(QLatin1Char(','), 0, 0).trimmed().toUpper();
            break;
        }
    }

    snap.keyboard = cached.isEmpty() ? QStringLiteral("—") : cached;
}

void refresh(EdenMetrics::Snapshot &snap)
{
    static qint64 lastMediaMs = 0;
    static QString media;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    readCpu(snap);
    readMemory(snap);
    readUptime(snap);
    readTemperatures(snap);
    readNetwork(snap);
    readBattery(snap);
    readKeyboard(snap);

    if (lastMediaMs == 0 || now - lastMediaMs >= 2000) {
        media = readMediaTitle();
        lastMediaMs = now;
    }

    snap.mediaTitle = media;
}

}  // namespace

namespace EdenMetrics {

const Snapshot &current()
{
    static Snapshot snap;
    static qint64 lastMs = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (lastMs != 0 && now - lastMs < 800) {
        return snap;
    }

    lastMs = now;
    Snapshot next = snap;
    refresh(next);
    snap = next;
    return snap;
}

QString formatRate(double bytesPerSec)
{
    if (bytesPerSec < 1024.0) {
        return QString::number(bytesPerSec, 'f', 0) + QStringLiteral(" B/s");
    }

    if (bytesPerSec < 1024.0 * 1024.0) {
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + QStringLiteral(" KB/s");
    }

    return QString::number(bytesPerSec / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB/s");
}

QString formatDuration(double seconds)
{
    const int total = std::max(0, static_cast<int>(seconds));
    const int days = total / 86400;
    const int hours = (total % 86400) / 3600;
    const int minutes = (total % 3600) / 60;

    if (days > 0) {
        return QStringLiteral("%1d %2h %3min").arg(days).arg(hours).arg(minutes);
    }

    if (hours > 0) {
        return QStringLiteral("%1h %2min").arg(hours).arg(minutes);
    }

    if (minutes > 0) {
        return QStringLiteral("%1min").arg(minutes);
    }

    return QStringLiteral("%1s").arg(total);
}

}  // namespace EdenMetrics
