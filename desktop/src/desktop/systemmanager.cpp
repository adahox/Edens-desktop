#include "desktop/systemmanager.hpp"
#include "desktop/dockbar.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QDateTime>
#include <QEvent>
#include <QSizePolicy>

#include <iterator>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sys/statvfs.h>

namespace {

constexpr QColor kBg = QColor(18, 22, 32, 235);
constexpr QColor kCard = QColor(28, 34, 48, 210);
constexpr QColor kCardBorder = QColor(70, 82, 110, 120);
constexpr QColor kText = QColor(242, 244, 248);
constexpr QColor kMuted = QColor(160, 170, 190);
constexpr QColor kCpu = QColor(0, 210, 255);
constexpr QColor kRam = QColor(196, 70, 255);
constexpr QColor kDisk = QColor(70, 230, 120);
constexpr QColor kNet = QColor(255, 170, 40);
constexpr QColor kGpu = QColor(80, 220, 140);

QString formatBytes(double bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double value = bytes;

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

int processCount()
{
    int count = 0;
    const QDir proc(QStringLiteral("/proc"));

    for (const QString &entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        entry.toInt(&ok);

        if (ok) {
            ++count;
        }
    }

    return count;
}

bool readCpuTimes(quint64 &total, quint64 &idle)
{
    QFile file(QStringLiteral("/proc/stat"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray line = file.readLine();
    const QList<QByteArray> parts = line.simplified().split(' ');

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

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
                iface.startsWith("br-") || iface == "virbr0") {
                continue;
            }

            const QList<QByteArray> parts = line.mid(colon + 1).simplified().split(' ');

            if (parts.size() < 9) {
                continue;
            }

            rx += parts.at(0).toULongLong();
            tx += parts.at(8).toULongLong();
        }
    }

    if (rx == 0 && tx == 0) {
        const QDir net(QStringLiteral("/sys/class/net"));

        for (const QString &iface : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (iface == QLatin1String("lo") || iface.startsWith(QLatin1String("docker")) ||
                iface.startsWith(QLatin1String("veth")) || iface.startsWith(QLatin1String("br-"))) {
                continue;
            }

            QFile rxFile(net.filePath(iface + QStringLiteral("/statistics/rx_bytes")));
            QFile txFile(net.filePath(iface + QStringLiteral("/statistics/tx_bytes")));

            if (rxFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                rx += rxFile.readAll().trimmed().toULongLong();
            }

            if (txFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                tx += txFile.readAll().trimmed().toULongLong();
            }
        }
    }

    return true;
}

double readCpuFrequencyGhz()
{
    double sumMhz = 0.0;
    int count = 0;
    const QDir cpus(QStringLiteral("/sys/devices/system/cpu"));

    for (const QString &entry : cpus.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith(QLatin1String("cpu"))) {
            continue;
        }

        bool ok = false;
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

    QFile file(QStringLiteral("/proc/cpuinfo"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0.0;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();

        if (!line.startsWith("cpu MHz")) {
            continue;
        }

        const auto parts = line.split(':');

        if (parts.size() >= 2) {
            sumMhz += parts.at(1).trimmed().toDouble();
            ++count;
        }
    }

    return count > 0 ? (sumMhz / count) / 1000.0 : 0.0;
}

double readGpuUsage()
{
    const QDir drm(QStringLiteral("/sys/class/drm"));

    for (const QString &entry : drm.entryList(QStringList{QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.contains(QLatin1Char('-'))) {
            continue;
        }

        QFile file(drm.filePath(entry + QStringLiteral("/device/gpu_busy_percent")));

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            bool ok = false;
            const double value = file.readAll().trimmed().toDouble(&ok);

            if (ok) {
                return value;
            }
        }
    }

    return -1.0;
}

double readGpuTemp()
{
    const QDir drm(QStringLiteral("/sys/class/drm"));

    for (const QString &entry : drm.entryList(QStringList{QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.contains(QLatin1Char('-'))) {
            continue;
        }

        const QDir hwmon(drm.filePath(entry + QStringLiteral("/device/hwmon")));

        for (const QString &sensor : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile tempFile(hwmon.filePath(sensor + QStringLiteral("/temp1_input")));

            if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const double millidegrees = tempFile.readAll().trimmed().toDouble();

                if (millidegrees > 0) {
                    return millidegrees / 1000.0;
                }
            }
        }
    }

    const QDir hwmon(QStringLiteral("/sys/class/hwmon"));

    for (const QString &entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile nameFile(hwmon.filePath(entry + QStringLiteral("/name")));

        if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QByteArray name = nameFile.readAll().trimmed().toLower();

        if (!name.contains("amdgpu") && !name.contains("nouveau") &&
            !name.contains("nvidia") && !name.contains("radeon")) {
            continue;
        }

        QFile tempFile(hwmon.filePath(entry + QStringLiteral("/temp1_input")));

        if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return tempFile.readAll().trimmed().toDouble() / 1000.0;
        }
    }

    return -1.0;
}

QString formatDegrees(double celsius)
{
    return QStringLiteral("%1 C").arg(celsius, 0, 'f', 0);
}

}  // namespace

class SystemManager::MetricCard final : public QFrame
{
public:
    MetricCard(
        const QString &title,
        const QColor &accent,
        bool showChart,
        QWidget *parent = nullptr)
        : QFrame(parent),
          accent_(accent),
          showChart_(showChart)
    {
        setObjectName(QStringLiteral("metricCard"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 14, 16, 12);
        layout->setSpacing(8);

        title_ = new QLabel(title, this);
        QFont titleFont = title_->font();
        titleFont.setPointSize(10);
        titleFont.setWeight(QFont::DemiBold);
        titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        title_->setFont(titleFont);
        title_->setStyleSheet(
            QStringLiteral("color: %1; background: transparent;").arg(accent.name()));
        layout->addWidget(title_);

        chartHost_ = new QWidget(this);
        chartHost_->setMinimumHeight(showChart ? 110 : 48);
        chartHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartHost_->installEventFilter(this);
        layout->addWidget(chartHost_, 1);

        stats_ = new QLabel(this);
        stats_->setWordWrap(true);
        stats_->setStyleSheet(QStringLiteral("color: #D7DCE8; background: transparent;"));
        QFont statsFont = stats_->font();
        statsFont.setPointSize(11);
        stats_->setFont(statsFont);
        layout->addWidget(stats_);

        history_.fill(0.0, 48);
    }

    void setStats(const QString &text)
    {
        stats_->setText(text);
    }

    void pushSample(double value)
    {
        if (!showChart_) {
            return;
        }

        if (history_.isEmpty()) {
            history_.fill(value, 48);
        } else {
            history_.removeFirst();
            history_.push_back(std::clamp(value, 0.0, 100.0));
        }

        chartHost_->update();
    }

    void setAccent(const QColor &accent)
    {
        accent_ = accent;
        update();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == chartHost_ && event->type() == QEvent::Paint) {
            paintChart();
            return true;
        }

        return QFrame::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 18, 18);
        painter.fillPath(path, kCard);
        painter.setPen(QPen(kCardBorder, 1));
        painter.drawPath(path);
    }

private:
    void paintChart()
    {
        QPainter painter(chartHost_);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(chartHost_->rect(), Qt::transparent);

        if (!showChart_ || history_.size() < 2) {
            painter.setPen(QPen(accent_, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(chartHost_->rect().adjusted(8, 8, -8, -8), 12, 12);
            return;
        }

        const QRect area = chartHost_->rect().adjusted(4, 8, -4, -4);
        QPainterPath line;
        QPainterPath fill;

        for (int index = 0; index < history_.size(); ++index) {
            const double ratio = history_.at(index) / 100.0;
            const double x = area.left() +
                (area.width() * static_cast<double>(index) / (history_.size() - 1));
            const double y = area.bottom() - (area.height() * ratio);

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
        gradient.setColorAt(0.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 140));
        gradient.setColorAt(1.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 10));
        painter.fillPath(fill, gradient);
        painter.setPen(QPen(accent_, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(line);
    }

    QLabel *title_ = nullptr;
    QLabel *stats_ = nullptr;
    QWidget *chartHost_ = nullptr;
    QColor accent_;
    bool showChart_ = true;
    QVector<double> history_;
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
            "QPushButton {"
            "  border: none;"
            "  border-radius: 12px;"
            "  color: #A8B0C2;"
            "  background: transparent;"
            "  font-size: 16px;"
            "}"
            "QPushButton:hover { background: rgba(255,255,255,0.06); color: #F2F4F8; }"
            "QPushButton:checked {"
            "  background: rgba(0, 210, 255, 0.14);"
            "  color: #00D2FF;"
            "  border-left: 3px solid #00D2FF;"
            "}"));
    }
};

SystemManager *SystemManager::active_ = nullptr;

SystemManager::SystemManager()
    : QWidget(nullptr)
{
    active_ = this;
    setWindowTitle(QStringLiteral("Gerenciador do Sistema"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(kWidth, kHeight);
    setStyleSheet(QStringLiteral(
        "SystemManager { background: #121620; color: #F2F4F8; }"
        "QLabel { background: transparent; }"));

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
        "border-radius: 14px;"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #00D2FF, stop:1 #39FF8A);"
        "color: #081018; font-weight: 800; font-size: 20px;"));
    sideLayout->addWidget(logo);
    sideLayout->addSpacing(12);

    const struct {
        const char *glyph;
        const char *tip;
    } sideItems[] = {
        {"⏱", "Desempenho"},
        {"⧉", "CPU"},
        {"▤", "Memória"},
        {"▣", "Disco"},
        {"≋", "Rede"},
        {"◉", "Wi-Fi"}
    };

    for (int index = 0; index < static_cast<int>(std::size(sideItems)); ++index) {
        auto *button = new SidebarButton(
            QString::fromUtf8(sideItems[index].glyph),
            QString::fromUtf8(sideItems[index].tip),
            sidebar_);
        sidebarButtons_.push_back(button);
        sideLayout->addWidget(button, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            setSection(index == 0 ? 0 : 0);
        });
    }

    sideLayout->addStretch();
    auto *settings = new SidebarButton(QStringLiteral("⚙"), QStringLiteral("Ajustes"), sidebar_);
    sideLayout->addWidget(settings, 0, Qt::AlignHCenter);
    root->addWidget(sidebar_);

    content_ = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(22, 18, 22, 18);
    contentLayout->setSpacing(16);

    auto *header = new QHBoxLayout;
    titleLabel_ = new QLabel(QStringLiteral("GERENCIADOR DE SISTEMA"), content_);
    QFont headerFont = titleLabel_->font();
    headerFont.setPointSize(16);
    headerFont.setWeight(QFont::Bold);
    titleLabel_->setFont(headerFont);
    titleLabel_->setStyleSheet(QStringLiteral("color: #F5F7FB;"));
    header->addWidget(titleLabel_);
    header->addStretch();

    const struct {
        const char *label;
        const char *icon;
    } tabs[] = {
        {"Desempenho", "⏱"},
        {"Serviços", "⚙"},
        {"Inicialização", "◷"},
        {"Rede", "🌐"}
    };

    for (int index = 0; index < static_cast<int>(std::size(tabs)); ++index) {
        auto *tab = new QPushButton(
            QString::fromUtf8(tabs[index].icon) + QLatin1Char(' ') +
                QString::fromUtf8(tabs[index].label),
            content_);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setCheckable(true);
        tab->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  border: none; border-radius: 14px; padding: 8px 14px;"
            "  color: #A8B0C2; background: transparent; font-size: 12px;"
            "}"
            "QPushButton:checked {"
            "  background: rgba(40, 110, 220, 0.85); color: white; font-weight: 600;"
            "}"
            "QPushButton:hover:!checked { background: rgba(255,255,255,0.05); color: #F2F4F8; }"));
        tabButtons_.push_back(tab);
        header->addWidget(tab);
        connect(tab, &QPushButton::clicked, this, [this, index]() {
            setSection(index);
        });
    }

    contentLayout->addLayout(header);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);

    cpuCard_ = new MetricCard(QStringLiteral("PROCESSADOR (CPU)"), kCpu, true, content_);
    ramChartCard_ = new MetricCard(QStringLiteral("MEMÓRIA RAM"), kRam, true, content_);
    ramDetailCard_ = new MetricCard(QStringLiteral("MEMÓRIA RAM"), kRam, true, content_);
    diskCard_ = new MetricCard(QStringLiteral("DISCO (SSD)"), kDisk, true, content_);
    netCard_ = new MetricCard(QStringLiteral("REDE"), kNet, true, content_);
    gpuCard_ = new MetricCard(QStringLiteral("PLACA DE VÍDEO (GPU)"), kGpu, false, content_);

    grid->addWidget(cpuCard_, 0, 0, 1, 3);
    grid->addWidget(ramChartCard_, 1, 0, 1, 2);
    grid->addWidget(ramDetailCard_, 1, 2, 1, 1);
    grid->addWidget(diskCard_, 2, 0, 1, 1);
    grid->addWidget(netCard_, 2, 1, 1, 1);
    grid->addWidget(gpuCard_, 2, 2, 1, 1);
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 2);
    grid->setRowStretch(2, 2);
    contentLayout->addLayout(grid, 1);
    root->addWidget(content_, 1);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() {
        refreshStats();
    });

    setSection(0);
    grabHotkey();
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

void SystemManager::toggle()
{
    if (isVisible()) {
        hide();
        timer_->stop();
        return;
    }

    placeOnScreen();
    refreshStats();
    show();
    raise();
    activateWindow();
    timer_->start(1000);

    if (DockBar *dock = DockBar::instance()) {
        dock->notifyOpening(
            QStringLiteral("Gerenciador do Sistema"),
            style()->standardIcon(QStyle::SP_ComputerIcon));
    }
}

void SystemManager::placeOnScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        return;
    }

    const QRect area = screen->availableGeometry();
    const int x = area.left() + (area.width() - width()) / 2;
    const int y = area.top() + (area.height() - height()) / 2;
    move(std::max(area.left(), x), std::max(area.top(), y));
}

void SystemManager::setSection(int section)
{
    section_ = section;

    for (int index = 0; index < tabButtons_.size(); ++index) {
        tabButtons_[index]->setChecked(index == section);
    }

    if (!sidebarButtons_.isEmpty()) {
        sidebarButtons_.first()->setChecked(section == 0);
    }

    const bool performance = section == 0;
    cpuCard_->setVisible(performance);
    ramChartCard_->setVisible(performance);
    ramDetailCard_->setVisible(performance);
    diskCard_->setVisible(performance);
    netCard_->setVisible(performance);
    gpuCard_->setVisible(performance);
}

void SystemManager::refreshStats()
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

    const double frequency = readCpuFrequencyGhz();
    const int processes = processCount();

    if (lastSampleMs_ > 0) {
        cpuCard_->pushSample(cpuPercent);
    }

    cpuCard_->setStats(
        QStringLiteral("Uso: %1%    Frequência: %2 GHz    Processos: %3")
            .arg(lastSampleMs_ > 0 ? QString::number(cpuPercent, 'f', 0) : QStringLiteral("--"))
            .arg(frequency > 0 ? QString::number(frequency, 'f', 2) : QStringLiteral("--"))
            .arg(processes));

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

    quint64 diskRead = 0;
    quint64 diskWrite = 0;
    double readRate = 0;
    double writeRate = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

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
            .arg(formatRate(std::max(0.0, readRate)))
            .arg(formatRate(std::max(0.0, writeRate))));

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

    const double netActivity = std::clamp((rxRate + txRate) / (8.0 * 1024.0 * 1024.0) * 100.0, 0.0, 100.0);
    netCard_->pushSample(netActivity);
    netCard_->setStats(
        QStringLiteral("Enviando: %1\nRecebendo: %2")
            .arg(formatRate(std::max(0.0, txRate)))
            .arg(formatRate(std::max(0.0, rxRate))));

    const double gpuUsage = readGpuUsage();
    const double gpuTemp = readGpuTemp();
    gpuCard_->setStats(
        QStringLiteral("Uso: %1\nTemp: %2")
            .arg(gpuUsage >= 0 ? QStringLiteral("%1%").arg(gpuUsage, 0, 'f', 0) : QStringLiteral("--"))
            .arg(gpuTemp >= 0 ? formatDegrees(gpuTemp) : QStringLiteral("--")));

    lastSampleMs_ = now;
}

void SystemManager::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(18, 22, 32));
}

void SystemManager::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshStats();
}

void SystemManager::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        timer_->stop();
        return;
    }

    QWidget::keyPressEvent(event);
}
