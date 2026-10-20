#include "desktop/networkmenu.hpp"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

QString runCapture(const QString &program, const QStringList &arguments)
{
    const QString executable = QStandardPaths::findExecutable(program);

    if (executable.isEmpty()) {
        return {};
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, arguments);

    if (!process.waitForStarted(2000) || !process.waitForFinished(5000)) {
        process.kill();
        return {};
    }

    return QString::fromUtf8(process.readAll()).trimmed();
}

bool runOk(const QString &program, const QStringList &arguments)
{
    const QString executable = QStandardPaths::findExecutable(program);

    if (executable.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(executable, arguments);
    return process.waitForStarted(2000) &&
           process.waitForFinished(8000) &&
           process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

QStringList nmcliLines(const QStringList &arguments)
{
    const QString output = runCapture(QStringLiteral("nmcli"), arguments);

    if (output.isEmpty()) {
        return {};
    }

    return output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

bool wifiRadioEnabled()
{
    const QString value = runCapture(
        QStringLiteral("nmcli"),
        {QStringLiteral("radio"), QStringLiteral("wifi")});
    return value.contains(QStringLiteral("enabled"), Qt::CaseInsensitive);
}

bool wifiHardwarePresent()
{
    for (const QString &line : nmcliLines({
             QStringLiteral("-t"),
             QStringLiteral("-f"),
             QStringLiteral("TYPE"),
             QStringLiteral("device")
         })) {
        if (line.compare(QStringLiteral("wifi"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

bool bluetoothAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("bluetoothctl")).isEmpty() &&
           !runCapture(QStringLiteral("bluetoothctl"), {QStringLiteral("list")}).isEmpty();
}

bool bluetoothPowered()
{
    const QString output = runCapture(
        QStringLiteral("bluetoothctl"),
        {QStringLiteral("show")});

    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("Powered:"))) {
            return line.contains(QStringLiteral("yes"), Qt::CaseInsensitive);
        }
    }

    return false;
}

struct WifiNetwork
{
    QString ssid;
    int signal = 0;
    bool active = false;
    QString security;
};

QVector<WifiNetwork> wifiNetworks()
{
    QVector<WifiNetwork> networks;
    QSet<QString> seen;

    for (const QString &line : nmcliLines({
             QStringLiteral("-t"),
             QStringLiteral("-f"),
             QStringLiteral("SSID,SIGNAL,IN-USE,SECURITY"),
             QStringLiteral("device"),
             QStringLiteral("wifi"),
             QStringLiteral("list")
         })) {
        const QStringList parts = line.split(QLatin1Char(':'));

        if (parts.size() < 3) {
            continue;
        }

        WifiNetwork network;
        network.ssid = parts.at(0).trimmed();

        if (network.ssid.isEmpty() || seen.contains(network.ssid)) {
            continue;
        }

        network.signal = parts.at(1).toInt();
        network.active = parts.at(2) == QLatin1String("*") ||
            parts.at(2).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        network.security = parts.size() > 3 ? parts.at(3) : QString();
        seen.insert(network.ssid);
        networks.push_back(network);
    }

    std::sort(networks.begin(), networks.end(), [](const WifiNetwork &left, const WifiNetwork &right) {
        if (left.active != right.active) {
            return left.active;
        }

        return left.signal > right.signal;
    });

    return networks;
}

struct VpnConnection
{
    QString name;
    bool active = false;
};

QVector<VpnConnection> vpnConnections()
{
    QVector<VpnConnection> connections;
    QSet<QString> active;

    for (const QString &line : nmcliLines({
             QStringLiteral("-t"),
             QStringLiteral("-f"),
             QStringLiteral("NAME,TYPE"),
             QStringLiteral("connection"),
             QStringLiteral("show"),
             QStringLiteral("--active")
         })) {
        const QStringList parts = line.split(QLatin1Char(':'));

        if (parts.size() >= 2 &&
            parts.at(1).contains(QStringLiteral("vpn"), Qt::CaseInsensitive)) {
            active.insert(parts.at(0));
        }
    }

    for (const QString &line : nmcliLines({
             QStringLiteral("-t"),
             QStringLiteral("-f"),
             QStringLiteral("NAME,TYPE"),
             QStringLiteral("connection"),
             QStringLiteral("show")
         })) {
        const QStringList parts = line.split(QLatin1Char(':'));

        if (parts.size() < 2 ||
            !parts.at(1).contains(QStringLiteral("vpn"), Qt::CaseInsensitive)) {
            continue;
        }

        VpnConnection connection;
        connection.name = parts.at(0);
        connection.active = active.contains(connection.name);
        connections.push_back(connection);
    }

    return connections;
}

struct WiredInfo
{
    QString device;
    QString connection;
    QString detail;
    bool connected = false;
    bool present = false;
};

QString &lastEthernetConnection()
{
    static QString name;
    return name;
}

WiredInfo wiredInfo()
{
    WiredInfo info;

    for (const QString &line : nmcliLines({
             QStringLiteral("-t"),
             QStringLiteral("-f"),
             QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"),
             QStringLiteral("device"),
             QStringLiteral("status")
         })) {
        const QStringList parts = line.split(QLatin1Char(':'));

        if (parts.size() < 4 ||
            parts.at(1).compare(QStringLiteral("ethernet"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        info.present = true;
        info.device = parts.at(0);
        const QString state = parts.at(2);
        QString connection = parts.at(3).trimmed();

        if (connection == QLatin1String("--")) {
            connection.clear();
        }

        info.connection = connection;
        info.connected =
            state.contains(QStringLiteral("connected"), Qt::CaseInsensitive) &&
            !state.contains(QStringLiteral("disconnected"), Qt::CaseInsensitive);

        if (!connection.isEmpty()) {
            lastEthernetConnection() = connection;
        }

        if (!connection.isEmpty()) {
            info.detail = connection;
        } else if (!lastEthernetConnection().isEmpty()) {
            info.detail = lastEthernetConnection();
            info.connection = lastEthernetConnection();
        } else {
            info.detail = info.connected ? QStringLiteral("Cabo conectado")
                                         : QStringLiteral("Cabo disponível");
        }

        return info;
    }

    return info;
}

bool setEthernetEnabled(const WiredInfo &wired, bool enable)
{
    if (wired.device.isEmpty()) {
        return false;
    }

    if (enable) {
        if (!wired.connection.isEmpty() &&
            runOk(
                QStringLiteral("nmcli"),
                {QStringLiteral("connection"), QStringLiteral("up"), wired.connection})) {
            return true;
        }

        return runOk(
            QStringLiteral("nmcli"),
            {QStringLiteral("device"), QStringLiteral("connect"), wired.device});
    }

    if (runOk(
            QStringLiteral("nmcli"),
            {QStringLiteral("device"), QStringLiteral("disconnect"), wired.device})) {
        return true;
    }

    if (!wired.connection.isEmpty()) {
        return runOk(
            QStringLiteral("nmcli"),
            {QStringLiteral("connection"), QStringLiteral("down"), wired.connection});
    }

    return false;
}

QPixmap blurredBackdrop(const QPixmap &source)
{
    if (source.isNull()) {
        return {};
    }

    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize tiny(
        std::max(1, image.width() / 12),
        std::max(1, image.height() / 12));
    image = image.scaled(tiny, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = image.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(image);
}

void drawKindIcon(QPainter &painter, NetworkMenu::Kind kind, const QRectF &box, const QColor &ink)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const qreal x = box.x();
    const qreal y = box.y();

    switch (kind) {
    case NetworkMenu::Kind::Wifi:
    case NetworkMenu::Kind::WifiNetwork:
        painter.drawArc(QRectF(x + 4, y + 5, 20, 20), 35 * 16, 110 * 16);
        painter.drawArc(QRectF(x + 7.5, y + 8.5, 13, 13), 35 * 16, 110 * 16);
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(x + 14, y + 20), 2.1, 2.1);
        break;
    case NetworkMenu::Kind::Bluetooth: {
        const QPointF top(x + 14, y + 4);
        const QPointF mid(x + 14, y + 14);
        const QPointF bottom(x + 14, y + 24);
        painter.drawLine(top, bottom);
        painter.drawLine(top, QPointF(x + 21, y + 9));
        painter.drawLine(QPointF(x + 21, y + 9), mid);
        painter.drawLine(mid, QPointF(x + 21, y + 19));
        painter.drawLine(QPointF(x + 21, y + 19), bottom);
        painter.drawLine(mid, QPointF(x + 7, y + 9));
        painter.drawLine(mid, QPointF(x + 7, y + 19));
        break;
    }
    case NetworkMenu::Kind::Ethernet:
        painter.drawRoundedRect(QRectF(x + 4, y + 7, 8, 6), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(x + 16, y + 7, 8, 6), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(x + 10, y + 17, 8, 6), 1.5, 1.5);
        painter.drawLine(QPointF(x + 8, y + 13), QPointF(x + 8, y + 15));
        painter.drawLine(QPointF(x + 20, y + 13), QPointF(x + 20, y + 15));
        painter.drawLine(QPointF(x + 8, y + 15), QPointF(x + 20, y + 15));
        painter.drawLine(QPointF(x + 14, y + 15), QPointF(x + 14, y + 17));
        break;
    case NetworkMenu::Kind::Vpn: {
        QPainterPath shield;
        shield.moveTo(x + 14, y + 3);
        shield.lineTo(x + 23, y + 7);
        shield.lineTo(x + 23, y + 15);
        shield.quadTo(x + 23, y + 22, x + 14, y + 25);
        shield.quadTo(x + 5, y + 22, x + 5, y + 15);
        shield.lineTo(x + 5, y + 7);
        shield.closeSubpath();
        painter.setBrush(QColor(10, 132, 255, 40));
        painter.setPen(QPen(QColor(QStringLiteral("#0A84FF")), 1.7));
        painter.drawPath(shield);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(x + 11.5, y + 12, 5, 6), 1.2, 1.2);
        painter.drawArc(QRectF(x + 11.8, y + 9.2, 4.4, 4.4), 0, 180 * 16);
        break;
    }
    }

    painter.restore();
}

class ToggleSwitch final : public QWidget
{
public:
    ToggleSwitch(bool on, bool enabled, const QColor &onColor, QWidget *parent = nullptr)
        : QWidget(parent)
        , on_(on)
        , enabled_(enabled)
        , progress_(on ? 1.0 : 0.0)
        , onColor_(onColor.isValid() ? onColor : QColor(QStringLiteral("#0A84FF")))
    {
        setFixedSize(40, 24);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setCursor(enabled_ ? Qt::PointingHandCursor : Qt::ArrowCursor);

        animation_ = new QVariantAnimation(this);
        animation_->setDuration(170);
        animation_->setEasingCurve(QEasingCurve::InOutCubic);
        connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress_ = value.toReal();
            update();
        });
        connect(animation_, &QVariantAnimation::finished, this, [this]() {
            if (!pendingCallback_) {
                return;
            }

            pendingCallback_ = false;

            if (callback_) {
                callback_();
            }
        });
    }

    void setCallback(const std::function<void()> &callback)
    {
        callback_ = callback;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QColor offTrack = enabled_ ? QColor(QStringLiteral("#C7C7CC"))
                                         : QColor(QStringLiteral("#D8D8DE"));
        const QColor onTrack = enabled_ ? onColor_ : QColor(QStringLiteral("#D8D8DE"));
        const QColor track(
            static_cast<int>(offTrack.red() + (onTrack.red() - offTrack.red()) * progress_),
            static_cast<int>(offTrack.green() + (onTrack.green() - offTrack.green()) * progress_),
            static_cast<int>(offTrack.blue() + (onTrack.blue() - offTrack.blue()) * progress_));

        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 12, 12);

        const qreal knob = 18.0;
        const qreal startX = 3.0;
        const qreal endX = width() - knob - 3.0;
        const qreal x = startX + (endX - startX) * progress_;
        painter.setBrush(Qt::white);
        painter.setPen(QPen(QColor(0, 0, 0, 24), 1.0));
        painter.drawEllipse(QRectF(x, 3.0, knob, knob));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !enabled_ || callback_ == nullptr) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        if (animation_->state() == QAbstractAnimation::Running) {
            return;
        }

        const qreal target = on_ ? 0.0 : 1.0;
        on_ = !on_;
        pendingCallback_ = true;
        animation_->stop();
        animation_->setStartValue(progress_);
        animation_->setEndValue(target);
        animation_->start();
        event->accept();
    }

private:
    bool on_ = false;
    bool enabled_ = true;
    bool pendingCallback_ = false;
    qreal progress_ = 0.0;
    QColor onColor_;
    std::function<void()> callback_;
    QVariantAnimation *animation_ = nullptr;
};

class ConnectionCard final : public QWidget
{
public:
    ConnectionCard(
        NetworkMenu::Kind kind,
        const QString &title,
        const QString &detail,
        bool enabled,
        bool available,
        bool active,
        const QColor &toggleOn,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , kind_(kind)
        , title_(title)
        , detail_(detail)
        , enabled_(enabled)
        , available_(available)
        , active_(active && available)
        , toggleOn_(toggleOn)
    {
        setFixedHeight(detail_.isEmpty() ? 48 : 54);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_OpaquePaintEvent, false);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 0, 10, 0);
        row->setSpacing(0);
        row->addStretch();

        if (!available_) {
            auto *status = new QLabel(QStringLiteral("Indisponível"));
            status->setAttribute(Qt::WA_TransparentForMouseEvents);
            status->setStyleSheet(QStringLiteral(
                "QLabel { color: #8E8E93; background: transparent; border: none; font-size: 11px; }"));
            row->addWidget(status);
            row->addSpacing(6);
        } else if (active_) {
            auto *check = new QLabel(QStringLiteral("✓"));
            check->setAttribute(Qt::WA_TransparentForMouseEvents);
            check->setStyleSheet(QStringLiteral(
                "QLabel { color: #0A84FF; background: transparent; border: none;"
                " font-size: 13px; font-weight: 700; }"));
            row->addWidget(check);
            row->addSpacing(6);
        }

        toggle_ = new ToggleSwitch(enabled_ && available_, available_, toggleOn_, this);
        row->addWidget(toggle_, 0, Qt::AlignVCenter);
    }

    void setToggleCallback(const std::function<void()> &callback)
    {
        if (toggle_ != nullptr) {
            toggle_->setCallback(callback);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Supersample the card plate so rounded edges stay smooth on X11.
        constexpr int scale = 3;
        QImage plate(width() * scale, height() * scale, QImage::Format_ARGB32_Premultiplied);
        plate.fill(Qt::transparent);
        QPainter platePainter(&plate);
        platePainter.setRenderHint(QPainter::Antialiasing);
        platePainter.scale(scale, scale);

        const QRectF box = QRectF(0, 0, width(), height()).adjusted(0.75, 0.75, -0.75, -0.75);
        QPainterPath path;
        path.addRoundedRect(box, 12, 12);
        platePainter.setPen(Qt::NoPen);
        platePainter.setBrush(QColor(248, 248, 250));
        platePainter.fillPath(path, QColor(248, 248, 250));
        platePainter.setPen(QPen(
            active_ ? QColor(QStringLiteral("#5AC8FA")) : QColor(QStringLiteral("#D2D2D7")),
            active_ ? 1.5 : 1.0));
        platePainter.setBrush(Qt::NoBrush);
        platePainter.drawPath(path);
        platePainter.end();

        painter.drawImage(
            rect(),
            plate.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

        const QRectF badge(10, (height() - 24) / 2.0, 24, 24);
        QPainterPath badgePath;
        badgePath.addRoundedRect(badge, 7, 7);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active_ ? QColor(10, 132, 255, 24) : QColor(236, 236, 241));
        painter.drawPath(badgePath);

        const QColor ink = active_ ? QColor(QStringLiteral("#0A84FF"))
                                   : QColor(QStringLiteral("#3A3A3C"));
        drawKindIcon(painter, kind_, badge, ink);

        const int textLeft = 42;
        const int textRight = width() - 100;
        QFont titleFont = font();
        titleFont.setPointSize(12);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#1C1C1E")));

        if (detail_.isEmpty()) {
            painter.drawText(
                QRect(textLeft, 0, textRight - textLeft, height()),
                Qt::AlignVCenter | Qt::AlignLeft,
                title_);
        } else {
            painter.drawText(
                QRect(textLeft, 8, textRight - textLeft, 20),
                Qt::AlignVCenter | Qt::AlignLeft,
                title_);
            QFont detailFont = font();
            detailFont.setPointSize(10);
            painter.setFont(detailFont);
            painter.setPen(QColor(QStringLiteral("#6E6E73")));
            painter.drawText(
                QRect(textLeft, 28, textRight - textLeft, 18),
                Qt::AlignVCenter | Qt::AlignLeft,
                detail_);
        }
    }

private:
    NetworkMenu::Kind kind_;
    QString title_;
    QString detail_;
    bool enabled_ = false;
    bool available_ = false;
    bool active_ = false;
    QColor toggleOn_;
    ToggleSwitch *toggle_ = nullptr;
};

}  // namespace

NetworkMenu::NetworkMenu(QWidget *anchor)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , anchor_(anchor)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFixedWidth(kWidth);
    setMouseTracking(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 8px; background: transparent; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #C7C7CC; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    body_ = new QWidget;
    body_->setStyleSheet(QStringLiteral("background: transparent;"));
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(0, 0, 0, 0);
    bodyLayout_->setSpacing(6);
    bodyLayout_->addStretch();
    scroll->setWidget(body_);
    root->addWidget(scroll);

    setMinimumHeight(220);
    setMaximumHeight(400);
    qApp->installEventFilter(this);
}

void NetworkMenu::open(QPushButton *anchor)
{
    if (anchor == nullptr) {
        return;
    }

    auto *menu = new NetworkMenu(anchor);
    menu->rebuild();
    menu->placeNearAnchor();
    menu->hide();
    menu->composePlate();
    menu->show();
    menu->raise();
}

void NetworkMenu::clearRows()
{
    while (bodyLayout_->count() > 0) {
        QLayoutItem *item = bodyLayout_->takeAt(0);

        if (item == nullptr) {
            continue;
        }

        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }

        delete item;
    }

    bodyLayout_->addStretch();
}

class HeaderIcon final : public QWidget
{
public:
    explicit HeaderIcon(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(236, 236, 241));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        drawKindIcon(
            painter,
            NetworkMenu::Kind::Ethernet,
            QRectF(0, 0, width(), height()),
            QColor(QStringLiteral("#3A3A3C")));
    }
};

void NetworkMenu::addHeader()
{
    auto *header = new QWidget;
    header->setFixedHeight(36);
    header->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(2, 0, 2, 0);
    row->setSpacing(10);

    row->addWidget(new HeaderIcon(header));

    auto *title = new QLabel(QStringLiteral("Conexões"));
    QFont font = title->font();
    font.setPointSize(15);
    font.setWeight(QFont::DemiBold);
    title->setFont(font);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: #1C1C1E; background: transparent; border: none; }"));
    row->addWidget(title);
    row->addStretch();

    bodyLayout_->insertWidget(bodyLayout_->count() - 1, header);

    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral(
        "QFrame { background: #D8D8DE; border: none; max-height: 1px; }"));
    bodyLayout_->insertWidget(bodyLayout_->count() - 1, line);
}

void NetworkMenu::addSection(const QString &title)
{
    auto *label = new QLabel(title.toUpper());
    QFont font = label->font();
    font.setPointSize(11);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #8E8E93; background: transparent; border: none;"
        " padding: 4px 4px 0 4px; }"));
    bodyLayout_->insertWidget(bodyLayout_->count() - 1, label);
}

void NetworkMenu::addCard(
    Kind kind,
    const QString &title,
    const QString &detail,
    bool enabled,
    bool available,
    bool active,
    const QColor &toggleOn,
    const std::function<void(bool)> &onToggle)
{
    auto *card = new ConnectionCard(
        kind,
        title,
        detail,
        enabled,
        available,
        active,
        toggleOn,
        body_);

    if (available) {
        card->setToggleCallback([this, enabled, onToggle]() {
            onToggle(!enabled);
            scheduleRefresh();
        });
    }

    bodyLayout_->insertWidget(bodyLayout_->count() - 1, card);
}

void NetworkMenu::scheduleRefresh()
{
    QTimer::singleShot(280, this, [this]() {
        rebuild();
        placeNearAnchor();
        composePlate();
        update();
    });
}

void NetworkMenu::runAndRefresh(const QString &program, const QStringList &arguments)
{
    runOk(program, arguments);
}

void NetworkMenu::rebuild()
{
    clearRows();
    addHeader();

    const bool hasWifi = wifiHardwarePresent();
    const bool wifiOn = hasWifi && wifiRadioEnabled();
    addCard(
        Kind::Wifi,
        QStringLiteral("Wi‑Fi"),
        {},
        wifiOn,
        hasWifi,
        wifiOn,
        QColor(QStringLiteral("#0A84FF")),
        [this](bool enable) {
            runAndRefresh(
                QStringLiteral("nmcli"),
                {QStringLiteral("radio"),
                 QStringLiteral("wifi"),
                 enable ? QStringLiteral("on") : QStringLiteral("off")});
        });

    const bool hasBluetooth = bluetoothAvailable();
    const bool bluetoothOn = hasBluetooth && bluetoothPowered();
    addCard(
        Kind::Bluetooth,
        QStringLiteral("Bluetooth"),
        {},
        bluetoothOn,
        hasBluetooth,
        bluetoothOn,
        QColor(QStringLiteral("#0A84FF")),
        [this](bool enable) {
            runAndRefresh(
                QStringLiteral("bluetoothctl"),
                {QStringLiteral("power"),
                 enable ? QStringLiteral("on") : QStringLiteral("off")});
        });

    const WiredInfo wired = wiredInfo();

    if (wired.present) {
        addCard(
            Kind::Ethernet,
            QStringLiteral("Ethernet"),
            wired.detail,
            wired.connected,
            true,
            wired.connected,
            QColor(QStringLiteral("#0A84FF")),
            [wired](bool enable) {
                setEthernetEnabled(wired, enable);
            });
    }

    const QVector<VpnConnection> vpns = vpnConnections();
    addSection(QStringLiteral("VPN"));

    if (vpns.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("Nenhuma VPN configurada"));
        empty->setStyleSheet(QStringLiteral(
            "QLabel { color: #AEAEB2; background: transparent; border: none;"
            " padding: 2px 6px 6px 6px; }"));
        bodyLayout_->insertWidget(bodyLayout_->count() - 1, empty);
    } else {
        for (const VpnConnection &vpn : vpns) {
            const QString name = vpn.name;
            const bool active = vpn.active;
            addCard(
                Kind::Vpn,
                name,
                active ? QStringLiteral("Conectada — clique para desligar")
                       : QStringLiteral("Clique para conectar"),
                active,
                true,
                active,
                QColor(QStringLiteral("#34C759")),
                [this, name, active](bool) {
                    runAndRefresh(
                        QStringLiteral("nmcli"),
                        {QStringLiteral("connection"),
                         active ? QStringLiteral("down") : QStringLiteral("up"),
                         name});
                });
        }
    }

    if (hasWifi && wifiOn) {
        addSection(QStringLiteral("Redes Wi‑Fi"));
        const QVector<WifiNetwork> networks = wifiNetworks();

        if (networks.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral("Nenhuma rede encontrada"));
            empty->setStyleSheet(QStringLiteral(
                "QLabel { color: #AEAEB2; background: transparent; border: none;"
                " padding: 2px 6px 6px 6px; }"));
            bodyLayout_->insertWidget(bodyLayout_->count() - 1, empty);
        } else {
            int shown = 0;

            for (const WifiNetwork &network : networks) {
                if (shown >= 8) {
                    break;
                }

                const QString ssid = network.ssid;
                const bool active = network.active;
                QString detail = QStringLiteral("%1%").arg(network.signal);

                if (!network.security.isEmpty() &&
                    network.security.compare(QStringLiteral("--"), Qt::CaseInsensitive) != 0) {
                    detail += QStringLiteral(" · ") + network.security;
                }

                addCard(
                    Kind::WifiNetwork,
                    ssid,
                    detail,
                    active,
                    true,
                    active,
                    QColor(QStringLiteral("#0A84FF")),
                    [this, ssid, active](bool enable) {
                        if (enable && !active) {
                            runAndRefresh(
                                QStringLiteral("nmcli"),
                                {QStringLiteral("device"),
                                 QStringLiteral("wifi"),
                                 QStringLiteral("connect"),
                                 ssid});
                        }
                    });
                ++shown;
            }
        }
    }

    adjustSize();
}

void NetworkMenu::placeNearAnchor()
{
    if (anchor_ == nullptr) {
        return;
    }

    const bool wasVisible = isVisible();

    if (wasVisible) {
        hide();
    }

    adjustSize();
    const int height = qBound(220, sizeHint().height() + 8, 400);
    setFixedHeight(height);

    const QPoint bottomRight = anchor_->mapToGlobal(QPoint(anchor_->width(), anchor_->height()));
    QRect screen(0, 0, 1280, 800);

    if (anchor_->screen() != nullptr) {
        screen = anchor_->screen()->geometry();
    }

    int x = bottomRight.x() - width();
    int y = bottomRight.y() + 8;
    x = qBound(screen.left() + 8, x, screen.right() - width() - 8);

    if (y + height > screen.bottom() - 8) {
        y = bottomRight.y() - height - 8;
    }

    move(x, y);

    if (wasVisible) {
        show();
    }
}

void NetworkMenu::composePlate()
{
    if (width() < 8 || height() < 8) {
        return;
    }

    clearMask();

    QPixmap behind;
    QScreen *screen = this->screen();

    if (screen == nullptr && anchor_ != nullptr) {
        screen = anchor_->screen();
    }

    if (screen != nullptr) {
        behind = screen->grabWindow(0, x(), y(), width(), height());
    }

    const QPixmap frosted = blurredBackdrop(behind);

    // Paint at 3x then downsample so glass edges stay smooth without a jagged X11 mask.
    constexpr int scale = 3;
    QImage sharp(width() * scale, height() * scale, QImage::Format_ARGB32_Premultiplied);
    sharp.fill(Qt::transparent);

    QPainter painter(&sharp);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);

    if (!frosted.isNull()) {
        painter.drawPixmap(rect(), frosted, frosted.rect());
    } else if (!behind.isNull()) {
        painter.drawPixmap(rect(), behind, behind.rect());
    } else {
        painter.fillRect(rect(), QColor(245, 245, 247));
    }

    const QRectF box(0.75, 0.75, width() - 1.5, height() - 1.5);
    QPainterPath shape;
    shape.addRoundedRect(box, kRadius, kRadius);
    painter.fillPath(shape, QColor(255, 255, 255, 214));
    painter.setPen(QPen(QColor(QStringLiteral("#C7C7CC")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
    painter.end();

    plate_ = QPixmap::fromImage(sharp.scaled(
        size(),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation));
    update();
}

void NetworkMenu::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    if (!plate_.isNull()) {
        painter.drawPixmap(0, 0, plate_);
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    painter.fillPath(shape, QColor(255, 255, 255, 235));
    painter.setPen(QPen(QColor(QStringLiteral("#C7C7CC")), 1.0));
    painter.drawPath(shape);
}

void NetworkMenu::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void NetworkMenu::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    composePlate();
}

bool NetworkMenu::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        const auto *mouse = static_cast<const QMouseEvent *>(event);
        const QPoint global = mouse->globalPosition().toPoint();

        if (!frameGeometry().contains(global) &&
            (anchor_ == nullptr || !anchor_->rect().contains(anchor_->mapFromGlobal(global)))) {
            close();
        }
    }

    return QWidget::eventFilter(watched, event);
}
