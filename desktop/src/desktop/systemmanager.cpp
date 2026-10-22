#include "desktop/systemmanager.hpp"
#include "desktop/dockbar.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <sys/statvfs.h>
#include <signal.h>
#include <unistd.h>

namespace {

constexpr QColor kBg(18, 24, 32);
constexpr QColor kCard(18, 24, 32, 217);
constexpr QColor kCardBorder(255, 255, 255, 38);
constexpr QColor kCpu(0, 229, 255);
constexpr QColor kRam(196, 70, 255);
constexpr QColor kDisk(70, 230, 120);
constexpr QColor kNet(255, 170, 40);
constexpr QColor kGpu(80, 220, 140);

const char *kTableStyle = R"(
QTableWidget {
    background: rgba(18, 24, 32, 0.85);
    alternate-background-color: rgba(255, 255, 255, 0.04);
    color: #E7F2F6;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 14px;
    gridline-color: rgba(255, 255, 255, 0.08);
    selection-background-color: rgba(0, 229, 255, 0.22);
    selection-color: #FFFFFF;
    font-size: 12px;
    outline: none;
}
QHeaderView::section {
    background: #121820;
    color: #8FA3B3;
    border: none;
    border-bottom: 1px solid rgba(255, 255, 255, 0.15);
    padding: 8px;
    font-weight: 600;
}
QTableWidget::item {
    color: #E7F2F6;
    background: transparent;
    padding: 6px;
}
QTableWidget::item:alternate {
    color: #E7F2F6;
    background: rgba(255, 255, 255, 0.04);
}
QTableWidget::item:selected {
    color: #121820;
    background: #00E5FF;
}
QTableWidget::item:selected:alternate {
    color: #121820;
    background: #00E5FF;
}
QLineEdit {
    background: rgba(255, 255, 255, 0.06);
    color: #E7F2F6;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 10px;
    padding: 8px 12px;
}
QPushButton[role="action"] {
    background: rgba(255, 255, 255, 0.06);
    color: #E7F2F6;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 10px;
    padding: 8px 14px;
}
QPushButton[role="action"]:hover { background: rgba(0, 229, 255, 0.16); }
QPushButton[role="danger"] {
    background: #5A2430;
    color: #FFD7DE;
    border: 1px solid #8A3A4A;
    border-radius: 10px;
    padding: 8px 14px;
}
QPushButton[role="danger"]:hover { background: #7A3040; }
)";

QString formatBytes(double bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double value = std::max(0.0, bytes);

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', value >= 10 || unit == 0 ? 1 : 2)
        .arg(QLatin1String(units[unit]));
}

QString formatRate(double bytesPerSecond)
{
    return formatBytes(bytesPerSecond) + QStringLiteral("/s");
}

QString formatDegrees(double celsius)
{
    return QStringLiteral("%1 C").arg(celsius, 0, 'f', 0);
}

int processCount()
{
    int count = 0;

    for (const QString &entry : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        entry.toInt(&ok);
        count += ok ? 1 : 0;
    }

    return count;
}

bool readCpuTimes(quint64 &total, quint64 &idle)
{
    QFile file(QStringLiteral("/proc/stat"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QList<QByteArray> parts = file.readLine().simplified().split(' ');

    if (parts.size() < 5 || parts.first() != "cpu") {
        return false;
    }

    total = 0;

    for (int index = 1; index < parts.size(); ++index) {
        total += parts.at(index).toULongLong();
    }

    idle = parts.at(4).toULongLong();

    if (parts.size() > 5) {
        idle += parts.at(5).toULongLong();
    }

    return total > 0;
}

bool readMemory(double &usedGb, double &availableGb, double &totalGb, double &percent)
{
    QFile file(QStringLiteral("/proc/meminfo"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    quint64 memTotal = 0;
    quint64 memAvailable = 0;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();

        if (line.startsWith("MemTotal:")) {
            memTotal = line.simplified().split(' ').value(1).toULongLong();
        } else if (line.startsWith("MemAvailable:")) {
            memAvailable = line.simplified().split(' ').value(1).toULongLong();
        }
    }

    if (memTotal == 0) {
        return false;
    }

    totalGb = memTotal / (1024.0 * 1024.0);
    availableGb = memAvailable / (1024.0 * 1024.0);
    usedGb = totalGb - availableGb;
    percent = (usedGb / totalGb) * 100.0;
    return true;
}

bool isWholeDisk(const QByteArray &name)
{
    if (name.startsWith("nvme")) {
        return name.contains('n') && !name.contains('p');
    }

    if (name.startsWith("sd") || name.startsWith("vd") || name.startsWith("hd")) {
        if (name.size() < 3) {
            return false;
        }

        for (int index = 2; index < name.size(); ++index) {
            if (!std::isalpha(static_cast<unsigned char>(name.at(index)))) {
                return false;
            }
        }

        return true;
    }

    return false;
}

bool readDiskRates(quint64 &readBytes, quint64 &writeBytes)
{
    QFile file(QStringLiteral("/proc/diskstats"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    readBytes = 0;
    writeBytes = 0;

    while (!file.atEnd()) {
        const QList<QByteArray> parts = file.readLine().simplified().split(' ');

        if (parts.size() < 14 || !isWholeDisk(parts.at(2))) {
            continue;
        }

        readBytes += parts.at(5).toULongLong() * 512ull;
        writeBytes += parts.at(9).toULongLong() * 512ull;
    }

    return true;
}

bool readDiskUsage(double &percent, double &usedGb, double &totalGb)
{
    struct statvfs stats {};

    if (statvfs("/", &stats) != 0 || stats.f_blocks == 0) {
        return false;
    }

    const double total = static_cast<double>(stats.f_blocks) * static_cast<double>(stats.f_frsize);
    const double free = static_cast<double>(stats.f_bavail) * static_cast<double>(stats.f_frsize);
    const double used = total - free;
    totalGb = total / (1024.0 * 1024.0 * 1024.0);
    usedGb = used / (1024.0 * 1024.0 * 1024.0);
    percent = (used / total) * 100.0;
    return true;
}

bool readNetworkRates(quint64 &rx, quint64 &tx)
{
    rx = 0;
    tx = 0;
    QFile file(QStringLiteral("/proc/net/dev"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    file.readLine();
    file.readLine();

    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        const int colon = line.indexOf(':');

        if (colon < 0) {
            continue;
        }

        const QByteArray iface = line.left(colon).trimmed();

        if (iface == "lo" || iface.startsWith("docker") || iface.startsWith("veth") ||
            iface.startsWith("br-") || iface.startsWith("virbr")) {
            continue;
        }

        const QList<QByteArray> parts = line.mid(colon + 1).simplified().split(' ');

        if (parts.size() < 9) {
            continue;
        }

        rx += parts.at(0).toULongLong();
        tx += parts.at(8).toULongLong();
    }

    return true;
}

double readCpuFrequencyGhz()
{
    double sumMhz = 0.0;
    int count = 0;
    const QDir cpus(QStringLiteral("/sys/devices/system/cpu"));

    for (const QString &entry : cpus.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;

        if (!entry.startsWith(QLatin1String("cpu"))) {
            continue;
        }

        entry.mid(3).toInt(&ok);

        if (!ok) {
            continue;
        }

        QFile file(cpus.filePath(entry + QStringLiteral("/cpufreq/scaling_cur_freq")));

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        sumMhz += file.readAll().trimmed().toDouble() / 1000.0;
        ++count;
    }

    if (count > 0) {
        return (sumMhz / count) / 1000.0;
    }

    return 0.0;
}

QStringList gpuCardDirs()
{
    QStringList cards;
    const QDir drm(QStringLiteral("/sys/class/drm"));

    for (const QString &entry :
         drm.entryList(QStringList{QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.contains(QLatin1Char('-'))) {
            continue;
        }

        cards.push_back(drm.filePath(entry));
    }

    // Prefer discrete cards (higher cardN often, and boot_vga=0).
    std::sort(cards.begin(), cards.end(), [](const QString &left, const QString &right) {
        QFile leftBoot(left + QStringLiteral("/device/boot_vga"));
        QFile rightBoot(right + QStringLiteral("/device/boot_vga"));
        int leftVga = 1;
        int rightVga = 1;

        if (leftBoot.open(QIODevice::ReadOnly | QIODevice::Text)) {
            leftVga = leftBoot.readAll().trimmed().toInt();
        }

        if (rightBoot.open(QIODevice::ReadOnly | QIODevice::Text)) {
            rightVga = rightBoot.readAll().trimmed().toInt();
        }

        if (leftVga != rightVga) {
            return leftVga < rightVga;
        }

        return left > right;
    });

    return cards;
}

double readSysfsPercent(const QString &path)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1.0;
    }

    bool ok = false;
    const double value = file.readAll().trimmed().toDouble(&ok);
    return ok ? value : -1.0;
}

double readNvidiaGpuUsage()
{
    QProcess process;
    process.start(
        QStringLiteral("nvidia-smi"),
        {QStringLiteral("--query-gpu=utilization.gpu"),
         QStringLiteral("--format=csv,noheader,nounits")});

    if (!process.waitForFinished(700) || process.exitCode() != 0) {
        return -1.0;
    }

    bool ok = false;
    const double value =
        QString::fromUtf8(process.readAllStandardOutput()).trimmed().split(QLatin1Char('\n')).value(0).toDouble(&ok);
    return ok ? value : -1.0;
}

double readGpuUsage()
{
    const double nvidia = readNvidiaGpuUsage();

    if (nvidia >= 0.0) {
        return nvidia;
    }

    const QStringList cards = gpuCardDirs();

    if (cards.isEmpty()) {
        return -1.0;
    }

    // amdgpu/i915 expose an instantaneous busy counter that is often 0 between
    // frames. Sample a short window and keep the peak so the UI reflects load.
    double peak = -1.0;

    for (int sample = 0; sample < 8; ++sample) {
        for (const QString &card : cards) {
            const double busy = readSysfsPercent(
                card + QStringLiteral("/device/gpu_busy_percent"));
            const double mem = readSysfsPercent(
                card + QStringLiteral("/device/mem_busy_percent"));
            const double intel = readSysfsPercent(
                card + QStringLiteral("/gt/gt0/busy_percentage"));

            peak = std::max(peak, busy);
            peak = std::max(peak, mem);
            peak = std::max(peak, intel);
        }

        if (sample + 1 < 8) {
            usleep(15000);
        }
    }

    return peak;
}

double readGpuTemp()
{
    QProcess nvidia;
    nvidia.start(
        QStringLiteral("nvidia-smi"),
        {QStringLiteral("--query-gpu=temperature.gpu"),
         QStringLiteral("--format=csv,noheader,nounits")});

    if (nvidia.waitForFinished(700) && nvidia.exitCode() == 0) {
        bool ok = false;
        const double value =
            QString::fromUtf8(nvidia.readAllStandardOutput()).trimmed().split(QLatin1Char('\n')).value(0).toDouble(&ok);

        if (ok) {
            return value;
        }
    }

    for (const QString &card : gpuCardDirs()) {
        const QDir hwmon(card + QStringLiteral("/device/hwmon"));

        for (const QString &sensor : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile tempFile(hwmon.filePath(sensor + QStringLiteral("/temp1_input")));

            if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const double value = tempFile.readAll().trimmed().toDouble();

                if (value > 0) {
                    return value / 1000.0;
                }
            }
        }
    }

    return -1.0;
}

double readGpuPowerWatts()
{
    for (const QString &card : gpuCardDirs()) {
        const QDir hwmon(card + QStringLiteral("/device/hwmon"));

        for (const QString &sensor : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile powerFile(hwmon.filePath(sensor + QStringLiteral("/power1_input")));

            if (!powerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            bool ok = false;
            const double microwatts = powerFile.readAll().trimmed().toDouble(&ok);

            if (ok && microwatts > 0) {
                return microwatts / 1000000.0;
            }
        }
    }

    return -1.0;
}

QString readProcessName(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/comm").arg(pid));

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll().trimmed());
    }

    return QStringLiteral("?");
}

QString readProcessUser(qint64 pid)
{
    QFileInfo info(QStringLiteral("/proc/%1").arg(pid));
    return info.owner();
}

quint64 readProcessRssKb(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/status").arg(pid));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();

        if (line.startsWith("VmRSS:")) {
            return line.simplified().split(' ').value(1).toULongLong();
        }
    }

    return 0;
}

quint64 readProcessCpuTime(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    const QByteArray line = file.readAll();
    const int close = line.indexOf(')');

    if (close < 0) {
        return 0;
    }

    const QList<QByteArray> parts = line.mid(close + 2).simplified().split(' ');

    if (parts.size() < 13) {
        return 0;
    }

    return parts.at(11).toULongLong() + parts.at(12).toULongLong();
}

bool runCommand(const QString &program, const QStringList &arguments, QString *output = nullptr)
{
    QProcess process;
    process.start(program, arguments);

    if (!process.waitForFinished(8000)) {
        process.kill();
        return false;
    }

    if (output != nullptr) {
        *output = QString::fromUtf8(process.readAllStandardOutput());
    }

    return process.exitCode() == 0;
}

QString desktopField(const QString &path, const QString &key)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (line.startsWith(key + QLatin1Char('='))) {
            return line.mid(key.size() + 1);
        }
    }

    return {};
}

bool writeDesktopHidden(const QString &path, bool hidden)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QStringList lines;
    bool found = false;

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());

        if (line.startsWith(QLatin1String("Hidden="))) {
            line = QStringLiteral("Hidden=%1\n").arg(hidden ? QStringLiteral("true") : QStringLiteral("false"));
            found = true;
        }

        lines.push_back(line);
    }

    file.close();

    if (!found) {
        lines.push_back(QStringLiteral("Hidden=%1\n").arg(hidden ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    for (const QString &line : lines) {
        file.write(line.toUtf8());
    }

    return true;
}

QPushButton *makeActionButton(const QString &text, const char *role, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("role", QLatin1String(role));
    return button;
}

}  // namespace

class SystemManager::MetricCard final : public QFrame
{
    class ChartView final : public QWidget
    {
    public:
        explicit ChartView(QWidget *parent = nullptr)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_OpaquePaintEvent, false);
            setAutoFillBackground(false);
            history_.fill(0.0, 48);
        }

        void setAccent(const QColor &accent)
        {
            accent_ = accent;
            update();
        }

        void setEnabledChart(bool enabled)
        {
            enabled_ = enabled;
            update();
        }

        void pushSample(double value)
        {
            if (!enabled_) {
                return;
            }

            const double clamped = std::clamp(value, 0.0, 100.0);

            if (!seeded_) {
                history_.fill(clamped);
                seeded_ = true;
            } else {
                history_.removeFirst();
                history_.push_back(clamped);
            }

            update();
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillRect(rect(), Qt::transparent);

            if (!enabled_ || width() < 8 || height() < 8) {
                return;
            }

            const QRect area = rect().adjusted(4, 6, -4, -4);

            painter.setPen(QPen(QColor(255, 255, 255, 18), 1));
            for (int step = 1; step <= 3; ++step) {
                const int y = area.top() + (area.height() * step) / 4;
                painter.drawLine(area.left(), y, area.right(), y);
            }

            if (history_.size() < 2) {
                return;
            }

            QPainterPath line;
            QPainterPath fill;

            for (int index = 0; index < history_.size(); ++index) {
                const double x = area.left() +
                    (area.width() * static_cast<double>(index) / (history_.size() - 1));
                const double y = area.bottom() - (area.height() * (history_.at(index) / 100.0));

                if (index == 0) {
                    line.moveTo(x, y);
                    fill.moveTo(x, area.bottom());
                    fill.lineTo(x, y);
                } else {
                    line.lineTo(x, y);
                    fill.lineTo(x, y);
                }
            }

            fill.lineTo(area.right(), area.bottom());
            fill.closeSubpath();

            QLinearGradient gradient(0, area.top(), 0, area.bottom());
            gradient.setColorAt(0.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 160));
            gradient.setColorAt(1.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 20));
            painter.fillPath(fill, gradient);
            painter.setPen(QPen(accent_, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(line);
        }

    private:
        QColor accent_ = Qt::cyan;
        QVector<double> history_;
        bool enabled_ = true;
        bool seeded_ = false;
    };

public:
    MetricCard(const QString &title, const QColor &accent, bool showChart, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 14, 16, 12);
        layout->setSpacing(8);

        title_ = new QLabel(title, this);
        QFont titleFont = title_->font();
        titleFont.setPointSize(10);
        titleFont.setWeight(QFont::DemiBold);
        title_->setFont(titleFont);
        title_->setStyleSheet(QStringLiteral("color: %1;").arg(accent.name()));
        layout->addWidget(title_);

        chart_ = new ChartView(this);
        chart_->setAccent(accent);
        chart_->setEnabledChart(showChart);
        chart_->setMinimumHeight(showChart ? 110 : 48);
        chart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget(chart_, 1);

        stats_ = new QLabel(this);
        stats_->setWordWrap(true);
        stats_->setStyleSheet(QStringLiteral("color: #D7DCE8;"));
        layout->addWidget(stats_);
    }

    void setStats(const QString &text)
    {
        stats_->setText(text);
    }

    void pushSample(double value)
    {
        chart_->pushSample(value);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 18, 18);
        painter.fillPath(path, kCard);
        painter.setPen(QPen(kCardBorder, 1));
        painter.drawPath(path);
    }

private:
    QLabel *title_ = nullptr;
    QLabel *stats_ = nullptr;
    ChartView *chart_ = nullptr;
};

class SystemManager::SidebarButton final : public QPushButton
{
public:
    SidebarButton(const QString &glyph, const QString &tip, QWidget *parent = nullptr)
        : QPushButton(glyph, parent)
    {
        setFixedSize(44, 44);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tip);
        setCheckable(true);
        setStyleSheet(QStringLiteral(
            "QPushButton { border: none; border-radius: 12px; color: #A8B0C2; background: transparent; font-size: 16px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.06); color: #F2F4F8; }"
            "QPushButton:checked { background: rgba(0,229,255,0.16); color: #00E5FF; border-left: 3px solid #00E5FF; }"));
    }
};

SystemManager *SystemManager::active_ = nullptr;

SystemManager::SystemManager()
    : QWidget(nullptr)
{
    active_ = this;
    setWindowTitle(QStringLiteral("Gerenciador do Sistema"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    resize(kWidth, kHeight);
    setStyleSheet(
        QStringLiteral("SystemManager { background: #121820; color: #E7F2F6; } QLabel { background: transparent; color: #E7F2F6; }") +
        QString::fromUtf8(kTableStyle));

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    sidebar_ = new QWidget(this);
    sidebar_->setFixedWidth(72);
    sidebar_->setStyleSheet(QStringLiteral("background: #0E121A;"));
    auto *sideLayout = new QVBoxLayout(sidebar_);
    sideLayout->setContentsMargins(14, 18, 14, 18);
    sideLayout->setSpacing(10);

    auto *logo = new QLabel(QStringLiteral("e"), sidebar_);
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(44, 44);
    logo->setStyleSheet(QStringLiteral(
        "border-radius: 14px; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #00D2FF, stop:1 #39FF8A);"
        "color: #081018; font-weight: 800; font-size: 20px;"));
    sideLayout->addWidget(logo);
    sideLayout->addSpacing(12);

    const struct {
        const char *glyph;
        const char *tip;
        int section;
    } sideItems[] = {
        {"☰", "Processos", Processes},
        {"⏱", "Desempenho", Performance},
        {"⚙", "Serviços", Services},
        {"◷", "Inicialização", Startup},
        {"≋", "Rede", Network}
    };

    for (const auto &item : sideItems) {
        auto *button = new SidebarButton(QString::fromUtf8(item.glyph), QString::fromUtf8(item.tip), sidebar_);
        sidebarButtons_.push_back(button);
        sideLayout->addWidget(button, 0, Qt::AlignHCenter);
        const int section = item.section;
        connect(button, &QPushButton::clicked, this, [this, section]() {
            setSection(section);
        });
    }

    sideLayout->addStretch();
    root->addWidget(sidebar_);

    content_ = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(22, 18, 22, 18);
    contentLayout->setSpacing(14);

    auto *header = new QHBoxLayout;
    titleLabel_ = new QLabel(QStringLiteral("GERENCIADOR DE SISTEMA"), content_);
    QFont headerFont = titleLabel_->font();
    headerFont.setPointSize(16);
    headerFont.setWeight(QFont::Bold);
    titleLabel_->setFont(headerFont);
    titleLabel_->setStyleSheet(QStringLiteral("color: #F5F7FB;"));
    header->addWidget(titleLabel_);
    header->addStretch();

    const char *tabs[] = {"Processos", "Desempenho", "Serviços", "Inicialização", "Rede"};

    for (int index = 0; index < SectionCount; ++index) {
        auto *tab = new QPushButton(QString::fromUtf8(tabs[index]), content_);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setCheckable(true);
        tab->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; border-radius: 14px; padding: 8px 14px; color: #A8B0C2; background: transparent; font-size: 12px; }"
            "QPushButton:checked { background: rgba(40,110,220,0.85); color: white; font-weight: 600; }"
            "QPushButton:hover:!checked { background: rgba(255,255,255,0.05); color: #F2F4F8; }"));
        tabButtons_.push_back(tab);
        header->addWidget(tab);
        connect(tab, &QPushButton::clicked, this, [this, index]() {
            setSection(index);
        });
    }

    contentLayout->addLayout(header);

    stack_ = new QStackedWidget(content_);

    QWidget *processToolbar = nullptr;
    auto *processPage = buildTablePage(
        QStringLiteral("Processos em execução"),
        &processTable_,
        {QStringLiteral("PID"), QStringLiteral("Nome"), QStringLiteral("Usuário"),
         QStringLiteral("CPU %"), QStringLiteral("Memória")},
        &processToolbar);
    processFilter_ = new QLineEdit(processToolbar);
    processFilter_->setPlaceholderText(QStringLiteral("Filtrar processos..."));
    processFilter_->setClearButtonEnabled(true);
    processSummary_ = new QLabel(processToolbar);
    processSummary_->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    auto *killButton = makeActionButton(QStringLiteral("Encerrar tarefa"), "danger", processToolbar);
    auto *refreshProcessesButton = makeActionButton(QStringLiteral("Atualizar"), "action", processToolbar);
    processToolbar->layout()->addWidget(processFilter_);
    processToolbar->layout()->addWidget(processSummary_);
    static_cast<QHBoxLayout *>(processToolbar->layout())->addStretch();
    processToolbar->layout()->addWidget(refreshProcessesButton);
    processToolbar->layout()->addWidget(killButton);
    connect(killButton, &QPushButton::clicked, this, [this]() {
        endSelectedProcess();
    });
    connect(refreshProcessesButton, &QPushButton::clicked, this, [this]() {
        refreshProcesses();
    });
    connect(processFilter_, &QLineEdit::textChanged, this, [this]() {
        refreshProcesses();
    });
    stack_->addWidget(processPage);

    performancePage_ = buildPerformancePage();
    stack_->addWidget(performancePage_);

    QWidget *serviceToolbar = nullptr;
    auto *servicePage = buildTablePage(
        QStringLiteral("Serviços do sistema"),
        &serviceTable_,
        {QStringLiteral("Serviço"), QStringLiteral("Estado"), QStringLiteral("Ativo"), QStringLiteral("Descrição")},
        &serviceToolbar);
    auto *startService = makeActionButton(QStringLiteral("Iniciar"), "action", serviceToolbar);
    auto *stopService = makeActionButton(QStringLiteral("Parar"), "danger", serviceToolbar);
    auto *restartService = makeActionButton(QStringLiteral("Reiniciar"), "action", serviceToolbar);
    auto *refreshServicesButton = makeActionButton(QStringLiteral("Atualizar"), "action", serviceToolbar);
    static_cast<QHBoxLayout *>(serviceToolbar->layout())->addStretch();
    serviceToolbar->layout()->addWidget(refreshServicesButton);
    serviceToolbar->layout()->addWidget(startService);
    serviceToolbar->layout()->addWidget(stopService);
    serviceToolbar->layout()->addWidget(restartService);
    connect(startService, &QPushButton::clicked, this, [this]() {
        controlSelectedService(QStringLiteral("start"));
    });
    connect(stopService, &QPushButton::clicked, this, [this]() {
        controlSelectedService(QStringLiteral("stop"));
    });
    connect(restartService, &QPushButton::clicked, this, [this]() {
        controlSelectedService(QStringLiteral("restart"));
    });
    connect(refreshServicesButton, &QPushButton::clicked, this, [this]() {
        refreshServices();
    });
    stack_->addWidget(servicePage);

    QWidget *startupToolbar = nullptr;
    auto *startupPage = buildTablePage(
        QStringLiteral("Programas de inicialização"),
        &startupTable_,
        {QStringLiteral("Nome"), QStringLiteral("Comando"), QStringLiteral("Estado"), QStringLiteral("Arquivo")},
        &startupToolbar);
    auto *enableStartup = makeActionButton(QStringLiteral("Ativar"), "action", startupToolbar);
    auto *disableStartup = makeActionButton(QStringLiteral("Desativar"), "danger", startupToolbar);
    auto *refreshStartupButton = makeActionButton(QStringLiteral("Atualizar"), "action", startupToolbar);
    static_cast<QHBoxLayout *>(startupToolbar->layout())->addStretch();
    startupToolbar->layout()->addWidget(refreshStartupButton);
    startupToolbar->layout()->addWidget(enableStartup);
    startupToolbar->layout()->addWidget(disableStartup);
    connect(enableStartup, &QPushButton::clicked, this, [this]() {
        toggleSelectedStartup(true);
    });
    connect(disableStartup, &QPushButton::clicked, this, [this]() {
        toggleSelectedStartup(false);
    });
    connect(refreshStartupButton, &QPushButton::clicked, this, [this]() {
        refreshStartup();
    });
    stack_->addWidget(startupPage);

    QWidget *networkToolbar = nullptr;
    auto *networkPage = buildTablePage(
        QStringLiteral("Interfaces de rede"),
        &networkTable_,
        {QStringLiteral("Interface"), QStringLiteral("Estado"), QStringLiteral("IPv4"),
         QStringLiteral("Recebido"), QStringLiteral("Enviado"), QStringLiteral("RX/s"), QStringLiteral("TX/s")},
        &networkToolbar);
    auto *refreshNetworkButton = makeActionButton(QStringLiteral("Atualizar"), "action", networkToolbar);
    static_cast<QHBoxLayout *>(networkToolbar->layout())->addStretch();
    networkToolbar->layout()->addWidget(refreshNetworkButton);
    connect(refreshNetworkButton, &QPushButton::clicked, this, [this]() {
        refreshNetwork();
    });
    stack_->addWidget(networkPage);

    contentLayout->addWidget(stack_, 1);
    root->addWidget(content_, 1);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() {
        refreshAll();
    });

    setSection(Processes);
    grabHotkey();
}

QWidget *SystemManager::buildPerformancePage()
{
    auto *page = new QWidget(stack_);
    auto *grid = new QGridLayout(page);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);

    cpuCard_ = new MetricCard(QStringLiteral("PROCESSADOR (CPU)"), kCpu, true, page);
    ramChartCard_ = new MetricCard(QStringLiteral("MEMÓRIA RAM"), kRam, true, page);
    ramDetailCard_ = new MetricCard(QStringLiteral("MEMÓRIA RAM"), kRam, true, page);
    diskCard_ = new MetricCard(QStringLiteral("DISCO"), kDisk, true, page);
    netCard_ = new MetricCard(QStringLiteral("REDE"), kNet, true, page);
    gpuCard_ = new MetricCard(QStringLiteral("PLACA DE VÍDEO (GPU)"), kGpu, true, page);

    grid->addWidget(cpuCard_, 0, 0, 1, 3);
    grid->addWidget(ramChartCard_, 1, 0, 1, 2);
    grid->addWidget(ramDetailCard_, 1, 2, 1, 1);
    grid->addWidget(diskCard_, 2, 0, 1, 1);
    grid->addWidget(netCard_, 2, 1, 1, 1);
    grid->addWidget(gpuCard_, 2, 2, 1, 1);
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 2);
    grid->setRowStretch(2, 2);
    return page;
}

QWidget *SystemManager::buildTablePage(
    const QString &title,
    QTableWidget **table,
    const QStringList &headers,
    QWidget **toolbarOut)
{
    auto *page = new QWidget(stack_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *heading = new QLabel(title, page);
    heading->setStyleSheet(QStringLiteral("color: #D7DCE8; font-size: 13px; font-weight: 600;"));
    layout->addWidget(heading);

    auto *toolbar = new QWidget(page);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);
    layout->addWidget(toolbar);
    *toolbarOut = toolbar;

    *table = new QTableWidget(0, headers.size(), page);
    (*table)->setHorizontalHeaderLabels(headers);
    (*table)->setSelectionBehavior(QAbstractItemView::SelectRows);
    (*table)->setSelectionMode(QAbstractItemView::SingleSelection);
    (*table)->setEditTriggers(QAbstractItemView::NoEditTriggers);
    (*table)->setAlternatingRowColors(true);
    (*table)->setShowGrid(false);
    (*table)->verticalHeader()->setVisible(false);
    (*table)->horizontalHeader()->setStretchLastSection(true);
    (*table)->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    (*table)->setSortingEnabled(true);

    QPalette tablePalette = (*table)->palette();
    tablePalette.setColor(QPalette::Base, QColor(QStringLiteral("#1C2230")));
    tablePalette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#242B3C")));
    tablePalette.setColor(QPalette::Text, QColor(QStringLiteral("#E8ECF4")));
    tablePalette.setColor(QPalette::HighlightedText, Qt::white);
    tablePalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2F6F4E")));
    (*table)->setPalette(tablePalette);

    layout->addWidget(*table, 1);
    return page;
}

void SystemManager::install()
{
    auto *manager = new SystemManager;
    qApp->installNativeEventFilter(manager);
}

SystemManager *SystemManager::instance()
{
    return active_;
}

void SystemManager::grabHotkey()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

    if (native == nullptr || native->connection() == nullptr) {
        return;
    }

    xcb_connection_t *connection = native->connection();
    const xcb_setup_t *setup = xcb_get_setup(connection);

    if (setup == nullptr) {
        return;
    }

    const xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;

    if (screen == nullptr) {
        return;
    }

    constexpr uint8_t kKeyEscape = 9;
    const uint16_t locks[] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)
    };

    for (const uint16_t lock : locks) {
        xcb_grab_key(
            connection,
            1,
            screen->root,
            static_cast<uint16_t>(XCB_MOD_MASK_CONTROL | lock),
            kKeyEscape,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC);
    }

    xcb_flush(connection);
}

bool SystemManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || message == nullptr) {
        return false;
    }

    const auto *event = static_cast<const xcb_generic_event_t *>(message);
    const uint8_t type = event->response_type & 0x7f;
    const auto *key = reinterpret_cast<const xcb_key_press_event_t *>(event);
    constexpr uint8_t kKeyEscape = 9;

    if (key->detail != kKeyEscape) {
        return false;
    }

    if (type == XCB_KEY_RELEASE) {
        lastRelease_ = key->time;
        haveRelease_ = true;
        return false;
    }

    if (type != XCB_KEY_PRESS || (key->state & XCB_MOD_MASK_CONTROL) == 0) {
        return false;
    }

    if (haveRelease_ && key->time - lastRelease_ < 40) {
        return true;
    }

    QTimer::singleShot(0, this, [this]() {
        toggle();
    });
    return true;
}

void SystemManager::dismiss()
{
    if (dismissing_) {
        return;
    }

    dismissing_ = true;
    timer_->stop();
    hide();

    if (DockBar *dock = DockBar::instance()) {
        dock->noteClosed(QStringLiteral("Gerenciador do Sistema"));
    }

    dismissing_ = false;
}

void SystemManager::toggle()
{
    if (isVisible()) {
        dismiss();
        return;
    }

    placeOnScreen();
    show();
    raise();
    activateWindow();
    refreshAll();
    timer_->start(1000);

    if (DockBar *dock = DockBar::instance()) {
        dock->notifyOpening(
            QStringLiteral("Gerenciador do Sistema"),
            style()->standardIcon(QStyle::SP_ComputerIcon),
            {},
            0,
            {QStringLiteral("Gerenciador do Sistema")});
    }
}

void SystemManager::placeOnScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        return;
    }

    const QRect area = screen->availableGeometry();
    move(
        area.left() + std::max(0, (area.width() - width()) / 2),
        area.top() + std::max(0, (area.height() - height()) / 2));
}

void SystemManager::setSection(int section)
{
    section_ = std::clamp(section, 0, SectionCount - 1);
    stack_->setCurrentIndex(section_);

    for (int index = 0; index < tabButtons_.size(); ++index) {
        tabButtons_[index]->setChecked(index == section_);
    }

    for (int index = 0; index < sidebarButtons_.size(); ++index) {
        sidebarButtons_[index]->setChecked(index == section_);
    }

    refreshAll();
}

void SystemManager::refreshAll()
{
    refreshPerformance();

    if (section_ == Processes) {
        refreshProcesses();
    } else if (section_ == Services) {
        refreshServices();
    } else if (section_ == Startup) {
        refreshStartup();
    } else if (section_ == Network) {
        refreshNetwork();
    }
}

void SystemManager::refreshPerformance()
{
    quint64 cpuTotal = 0;
    quint64 cpuIdle = 0;
    double cpuPercent = 0.0;

    if (readCpuTimes(cpuTotal, cpuIdle)) {
        if (lastCpuTotal_ > 0 && cpuTotal > lastCpuTotal_) {
            const double totalDelta = static_cast<double>(cpuTotal - lastCpuTotal_);
            const double idleDelta = static_cast<double>(cpuIdle - lastCpuIdle_);
            cpuPercent = std::clamp((1.0 - idleDelta / totalDelta) * 100.0, 0.0, 100.0);
        }

        lastCpuTotal_ = cpuTotal;
        lastCpuIdle_ = cpuIdle;
    }

    if (lastSampleMs_ > 0) {
        cpuCard_->pushSample(cpuPercent);
    }

    cpuCard_->setStats(
        QStringLiteral("Uso: %1%    Frequência: %2 GHz    Processos: %3")
            .arg(lastSampleMs_ > 0 ? QString::number(cpuPercent, 'f', 0) : QStringLiteral("--"))
            .arg(QString::number(readCpuFrequencyGhz(), 'f', 2))
            .arg(processCount()));

    double usedGb = 0;
    double availableGb = 0;
    double totalGb = 0;
    double ramPercent = 0;

    if (readMemory(usedGb, availableGb, totalGb, ramPercent)) {
        ramChartCard_->pushSample(ramPercent);
        ramDetailCard_->pushSample(ramPercent);
        ramChartCard_->setStats(
            QStringLiteral("%1 GB em uso (%2%)\nDisponível: %3 GB")
                .arg(usedGb, 0, 'f', 1)
                .arg(ramPercent, 0, 'f', 0)
                .arg(availableGb, 0, 'f', 1));
        ramDetailCard_->setStats(
            QStringLiteral("Uso: %1%\nUso: %2 GB\nDisponível: %3 GB\nTotal: %4 GB")
                .arg(ramPercent, 0, 'f', 0)
                .arg(usedGb, 0, 'f', 1)
                .arg(availableGb, 0, 'f', 1)
                .arg(totalGb, 0, 'f', 1));
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    quint64 diskRead = 0;
    quint64 diskWrite = 0;
    double readRate = 0;
    double writeRate = 0;

    if (readDiskRates(diskRead, diskWrite)) {
        if (lastSampleMs_ > 0 && diskRead >= lastDiskRead_ && diskWrite >= lastDiskWrite_) {
            const double seconds = std::max(0.001, (now - lastSampleMs_) / 1000.0);
            readRate = (diskRead - lastDiskRead_) / seconds;
            writeRate = (diskWrite - lastDiskWrite_) / seconds;
        }

        lastDiskRead_ = diskRead;
        lastDiskWrite_ = diskWrite;
    }

    double diskPercent = 0;
    double diskUsedGb = 0;
    double diskTotalGb = 0;
    readDiskUsage(diskPercent, diskUsedGb, diskTotalGb);
    diskCard_->pushSample(diskPercent);
    diskCard_->setStats(
        QStringLiteral("Uso: %1% (%2 / %3 GB)\nLeitura: %4\nEscrita: %5")
            .arg(diskPercent, 0, 'f', 0)
            .arg(diskUsedGb, 0, 'f', 1)
            .arg(diskTotalGb, 0, 'f', 1)
            .arg(formatRate(readRate))
            .arg(formatRate(writeRate)));

    quint64 netRx = 0;
    quint64 netTx = 0;
    double rxRate = 0;
    double txRate = 0;

    if (readNetworkRates(netRx, netTx)) {
        if (lastSampleMs_ > 0 && netRx >= lastNetRx_ && netTx >= lastNetTx_) {
            const double seconds = std::max(0.001, (now - lastSampleMs_) / 1000.0);
            rxRate = (netRx - lastNetRx_) / seconds;
            txRate = (netTx - lastNetTx_) / seconds;
        }

        lastNetRx_ = netRx;
        lastNetTx_ = netTx;
    }

    netCard_->pushSample(std::clamp((rxRate + txRate) / (8.0 * 1024.0 * 1024.0) * 100.0, 0.0, 100.0));
    netCard_->setStats(
        QStringLiteral("Enviando: %1\nRecebendo: %2")
            .arg(formatRate(txRate))
            .arg(formatRate(rxRate)));

    const double gpuUsage = readGpuUsage();
    const double gpuTemp = readGpuTemp();
    const double gpuPower = readGpuPowerWatts();
    const double gpuSample = gpuUsage >= 0.0 ? gpuUsage : 0.0;
    gpuCard_->pushSample(gpuSample);

    QStringList gpuLines;
    gpuLines << QStringLiteral("Uso: %1").arg(
        gpuUsage >= 0 ? QStringLiteral("%1%").arg(gpuUsage, 0, 'f', 0) : QStringLiteral("--"));
    gpuLines << QStringLiteral("Temp: %1").arg(
        gpuTemp >= 0 ? formatDegrees(gpuTemp) : QStringLiteral("--"));

    if (gpuPower >= 0) {
        gpuLines << QStringLiteral("Energia: %1 W").arg(gpuPower, 0, 'f', 1);
    }

    gpuCard_->setStats(gpuLines.join(QLatin1Char('\n')));

    lastSampleMs_ = now;
}

void SystemManager::refreshProcesses()
{
    if (processTable_ == nullptr) {
        return;
    }

    quint64 cpuTotal = 0;
    quint64 cpuIdle = 0;
    readCpuTimes(cpuTotal, cpuIdle);
    const quint64 totalDelta =
        lastProcessCpuTotal_ > 0 && cpuTotal > lastProcessCpuTotal_
            ? cpuTotal - lastProcessCpuTotal_
            : 0;

    struct Row {
        qint64 pid = 0;
        QString name;
        QString user;
        double cpu = 0;
        double memoryMb = 0;
    };

    QVector<Row> rows;
    QHash<qint64, quint64> nextCpu;
    const QString filter = processFilter_ != nullptr ? processFilter_->text().trimmed().toLower() : QString();

    for (const QString &entry : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);

        if (!ok) {
            continue;
        }

        const QString name = readProcessName(pid);

        if (name.isEmpty() || name == QLatin1String("?")) {
            continue;
        }

        if (!filter.isEmpty() &&
            !name.toLower().contains(filter) &&
            !entry.contains(filter)) {
            continue;
        }

        const quint64 cpuTime = readProcessCpuTime(pid);
        nextCpu.insert(pid, cpuTime);
        double cpuPercent = 0.0;

        if (totalDelta > 0 && lastProcessCpu_.contains(pid)) {
            const quint64 delta = cpuTime >= lastProcessCpu_.value(pid)
                ? cpuTime - lastProcessCpu_.value(pid)
                : 0;
            cpuPercent = (static_cast<double>(delta) / static_cast<double>(totalDelta)) * 100.0;
        }

        Row row;
        row.pid = pid;
        row.name = name;
        row.user = readProcessUser(pid);
        row.cpu = cpuPercent;
        row.memoryMb = readProcessRssKb(pid) / 1024.0;
        rows.push_back(row);
    }

    lastProcessCpu_ = nextCpu;
    lastProcessCpuTotal_ = cpuTotal;

    std::sort(rows.begin(), rows.end(), [](const Row &left, const Row &right) {
        return left.cpu > right.cpu;
    });

    const qint64 selectedPid =
        processTable_->currentRow() >= 0
            ? processTable_->item(processTable_->currentRow(), 0)->text().toLongLong()
            : -1;

    processTable_->setSortingEnabled(false);
    processTable_->setRowCount(rows.size());

    for (int index = 0; index < rows.size(); ++index) {
        const Row &row = rows.at(index);
        processTable_->setItem(index, 0, new QTableWidgetItem(QString::number(row.pid)));
        processTable_->setItem(index, 1, new QTableWidgetItem(row.name));
        processTable_->setItem(index, 2, new QTableWidgetItem(row.user));

        auto *cpuItem = new QTableWidgetItem(QString::number(row.cpu, 'f', 1));
        cpuItem->setData(Qt::DisplayRole, row.cpu);
        processTable_->setItem(index, 3, cpuItem);

        auto *memItem = new QTableWidgetItem(QStringLiteral("%1 MB").arg(row.memoryMb, 0, 'f', 1));
        memItem->setData(Qt::UserRole, row.memoryMb);
        processTable_->setItem(index, 4, memItem);

        if (row.pid == selectedPid) {
            processTable_->selectRow(index);
        }
    }

    processTable_->setSortingEnabled(true);

    if (processSummary_ != nullptr) {
        processSummary_->setText(QStringLiteral("%1 processos").arg(rows.size()));
    }
}

void SystemManager::refreshServices()
{
    if (serviceTable_ == nullptr) {
        return;
    }

    QString output;
    const bool ok = runCommand(
        QStringLiteral("systemctl"),
        {QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all"),
         QStringLiteral("--no-pager"), QStringLiteral("--no-legend"), QStringLiteral("--plain")},
        &output);

    serviceTable_->setSortingEnabled(false);
    serviceTable_->setRowCount(0);

    if (!ok) {
        serviceTable_->setRowCount(1);
        serviceTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("systemctl indisponível")));
        serviceTable_->setSortingEnabled(true);
        return;
    }

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    int row = 0;
    serviceTable_->setRowCount(lines.size());

    for (const QString &line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

        if (parts.size() < 4) {
            continue;
        }

        const QString unit = parts.at(0);
        const QString load = parts.at(1);
        const QString active = parts.at(2);
        const QString description = parts.mid(4).join(QLatin1Char(' '));
        serviceTable_->setItem(row, 0, new QTableWidgetItem(unit));
        serviceTable_->setItem(row, 1, new QTableWidgetItem(load));
        serviceTable_->setItem(row, 2, new QTableWidgetItem(active));
        serviceTable_->setItem(row, 3, new QTableWidgetItem(description.isEmpty() ? parts.value(3) : description));
        ++row;
    }

    serviceTable_->setRowCount(row);
    serviceTable_->setSortingEnabled(true);
}

void SystemManager::refreshStartup()
{
    if (startupTable_ == nullptr) {
        return;
    }

    const QStringList directories = {
        QDir::home().filePath(QStringLiteral(".config/autostart")),
        QStringLiteral("/etc/xdg/autostart")
    };

    struct Entry {
        QString name;
        QString command;
        QString state;
        QString path;
    };

    QVector<Entry> entries;

    for (const QString &directory : directories) {
        const QDir dir(directory);

        if (!dir.exists()) {
            continue;
        }

        for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const QString path = info.absoluteFilePath();
            const QString hidden = desktopField(path, QStringLiteral("Hidden")).toLower();
            const QString name = desktopField(path, QStringLiteral("Name"));
            const QString command = desktopField(path, QStringLiteral("Exec"));

            if (name.isEmpty() && command.isEmpty()) {
                continue;
            }

            Entry entry;
            entry.name = name.isEmpty() ? info.baseName() : name;
            entry.command = command;
            entry.state = (hidden == QLatin1String("true") || hidden == QLatin1String("1"))
                ? QStringLiteral("Desativado")
                : QStringLiteral("Ativado");
            entry.path = path;
            entries.push_back(entry);
        }
    }

    startupTable_->setSortingEnabled(false);
    startupTable_->setRowCount(entries.size());

    for (int index = 0; index < entries.size(); ++index) {
        const Entry &entry = entries.at(index);
        startupTable_->setItem(index, 0, new QTableWidgetItem(entry.name));
        startupTable_->setItem(index, 1, new QTableWidgetItem(entry.command));
        startupTable_->setItem(index, 2, new QTableWidgetItem(entry.state));
        auto *pathItem = new QTableWidgetItem(entry.path);
        pathItem->setData(Qt::UserRole, entry.path);
        startupTable_->setItem(index, 3, pathItem);
    }

    startupTable_->setSortingEnabled(true);
}

void SystemManager::refreshNetwork()
{
    if (networkTable_ == nullptr) {
        return;
    }

    struct Iface {
        QString name;
        QString state;
        QString ipv4;
        quint64 rx = 0;
        quint64 tx = 0;
    };

    QVector<Iface> interfaces;
    const QDir net(QStringLiteral("/sys/class/net"));

    for (const QString &name : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name == QLatin1String("lo")) {
            continue;
        }

        Iface iface;
        iface.name = name;

        QFile oper(net.filePath(name + QStringLiteral("/operstate")));

        if (oper.open(QIODevice::ReadOnly | QIODevice::Text)) {
            iface.state = QString::fromUtf8(oper.readAll().trimmed());
        }

        QFile rxFile(net.filePath(name + QStringLiteral("/statistics/rx_bytes")));
        QFile txFile(net.filePath(name + QStringLiteral("/statistics/tx_bytes")));

        if (rxFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            iface.rx = rxFile.readAll().trimmed().toULongLong();
        }

        if (txFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            iface.tx = txFile.readAll().trimmed().toULongLong();
        }

        QString ipOutput;
        runCommand(
            QStringLiteral("ip"),
            {QStringLiteral("-4"), QStringLiteral("-o"), QStringLiteral("addr"),
             QStringLiteral("show"), QStringLiteral("dev"), name},
            &ipOutput);
        const QStringList ipParts = ipOutput.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const int inetIndex = ipParts.indexOf(QStringLiteral("inet"));

        if (inetIndex >= 0 && inetIndex + 1 < ipParts.size()) {
            iface.ipv4 = ipParts.at(inetIndex + 1).section(QLatin1Char('/'), 0, 0);
        }

        interfaces.push_back(iface);
    }

    static QHash<QString, quint64> lastRx;
    static QHash<QString, quint64> lastTx;
    static qint64 lastNetMs = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds = lastNetMs > 0 ? std::max(0.001, (now - lastNetMs) / 1000.0) : 0.0;

    networkTable_->setSortingEnabled(false);
    networkTable_->setRowCount(interfaces.size());

    for (int index = 0; index < interfaces.size(); ++index) {
        const Iface &iface = interfaces.at(index);
        double rxRate = 0;
        double txRate = 0;

        if (seconds > 0 && lastRx.contains(iface.name) && lastTx.contains(iface.name)) {
            rxRate = (iface.rx - lastRx.value(iface.name)) / seconds;
            txRate = (iface.tx - lastTx.value(iface.name)) / seconds;
        }

        lastRx.insert(iface.name, iface.rx);
        lastTx.insert(iface.name, iface.tx);

        networkTable_->setItem(index, 0, new QTableWidgetItem(iface.name));
        networkTable_->setItem(index, 1, new QTableWidgetItem(iface.state));
        networkTable_->setItem(index, 2, new QTableWidgetItem(iface.ipv4.isEmpty() ? QStringLiteral("--") : iface.ipv4));
        networkTable_->setItem(index, 3, new QTableWidgetItem(formatBytes(static_cast<double>(iface.rx))));
        networkTable_->setItem(index, 4, new QTableWidgetItem(formatBytes(static_cast<double>(iface.tx))));
        networkTable_->setItem(index, 5, new QTableWidgetItem(formatRate(std::max(0.0, rxRate))));
        networkTable_->setItem(index, 6, new QTableWidgetItem(formatRate(std::max(0.0, txRate))));
    }

    lastNetMs = now;
    networkTable_->setSortingEnabled(true);
}

void SystemManager::endSelectedProcess()
{
    if (processTable_ == nullptr || processTable_->currentRow() < 0) {
        return;
    }

    const qint64 pid = processTable_->item(processTable_->currentRow(), 0)->text().toLongLong();
    const QString name = processTable_->item(processTable_->currentRow(), 1)->text();

    if (pid <= 1 || pid == static_cast<qint64>(getpid())) {
        QMessageBox::warning(this, QStringLiteral("Processos"), QStringLiteral("Esse processo não pode ser encerrado."));
        return;
    }

    if (QMessageBox::question(
            this,
            QStringLiteral("Encerrar tarefa"),
            QStringLiteral("Encerrar \"%1\" (PID %2)?").arg(name).arg(pid)) != QMessageBox::Yes) {
        return;
    }

    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0) {
        QMessageBox::warning(this, QStringLiteral("Processos"), QStringLiteral("Não foi possível encerrar o processo."));
        return;
    }

    QTimer::singleShot(400, this, [this]() {
        refreshProcesses();
    });
}

void SystemManager::controlSelectedService(const QString &action)
{
    if (serviceTable_ == nullptr || serviceTable_->currentRow() < 0) {
        return;
    }

    const QString unit = serviceTable_->item(serviceTable_->currentRow(), 0)->text();

    if (unit.isEmpty()) {
        return;
    }

    QString error;
    QProcess process;
    process.start(QStringLiteral("systemctl"), {action, unit});

    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed();

        if (error.isEmpty()) {
            error = QStringLiteral("Falha ao executar systemctl %1 %2").arg(action, unit);
        }

        QMessageBox::warning(this, QStringLiteral("Serviços"), error);
        return;
    }

    refreshServices();
}

void SystemManager::toggleSelectedStartup(bool enable)
{
    if (startupTable_ == nullptr || startupTable_->currentRow() < 0) {
        return;
    }

    const QString path =
        startupTable_->item(startupTable_->currentRow(), 3)->data(Qt::UserRole).toString();

    if (path.isEmpty()) {
        return;
    }

    if (!path.startsWith(QDir::homePath()) && enable == false) {
        QMessageBox::warning(
            this,
            QStringLiteral("Inicialização"),
            QStringLiteral("Só é possível alterar entradas do seu usuário (~/.config/autostart)."));
        return;
    }

    if (!writeDesktopHidden(path, !enable)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Inicialização"),
            QStringLiteral("Não consegui atualizar %1").arg(path));
        return;
    }

    refreshStartup();
}

void SystemManager::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), kBg);
}

void SystemManager::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshAll();
}

void SystemManager::closeEvent(QCloseEvent *event)
{
    event->accept();
    dismiss();
}

void SystemManager::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }

    if (event->key() == Qt::Key_Delete && section_ == Processes) {
        endSelectedProcess();
        return;
    }

    QWidget::keyPressEvent(event);
}
