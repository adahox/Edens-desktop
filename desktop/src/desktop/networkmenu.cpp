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
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QRegion>
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
#include <memory>
#include <utility>

namespace {

QStringList splitLines(const QString &output)
{
    if (output.isEmpty()) {
        return {};
    }

    return output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

bool wifiRadioEnabled(const QString &value)
{
    return value.contains(QStringLiteral("enabled"), Qt::CaseInsensitive);
}

bool wifiHardwarePresent(const QString &output)
{
    for (const QString &line : splitLines(output)) {
        if (line.compare(QStringLiteral("wifi"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

bool bluetoothAvailable(const QString &listOutput)
{
    return !QStandardPaths::findExecutable(QStringLiteral("bluetoothctl")).isEmpty() &&
           !listOutput.trimmed().isEmpty();
}

bool bluetoothPowered(const QString &output)
{
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("Powered:"))) {
            return line.contains(QStringLiteral("yes"), Qt::CaseInsensitive);
        }
    }

    return false;
}

void captureMany(
    QObject *context,
    const QVector<QPair<QString, QStringList>> &jobs,
    const std::function<void(QVector<QString>)> &done)
{
    auto remaining = std::make_shared<int>(jobs.size());
    auto outputs = std::make_shared<QVector<QString>>(jobs.size());
    auto settled = std::make_shared<QVector<char>>(jobs.size(), 0);

    if (jobs.isEmpty()) {
        done({});
        return;
    }

    const auto finishOne = [=](int index, const QString &text) {
        if (settled->at(index) != 0) {
            return;
        }

        (*settled)[index] = 1;
        (*outputs)[index] = text;

        if (--(*remaining) == 0 && done) {
            done(*outputs);
        }
    };

    for (int index = 0; index < jobs.size(); ++index) {
        const QString executable = QStandardPaths::findExecutable(jobs.at(index).first);

        if (executable.isEmpty()) {
            finishOne(index, {});
            continue;
        }

        auto *process = new QProcess(context);
        process->setProcessChannelMode(QProcess::MergedChannels);
        const auto settle = [process, finishOne, index](const QString &text) {
            const QString output = text;
            process->deleteLater();
            finishOne(index, output);
        };
        QObject::connect(process, &QProcess::finished, context, [process, settle](int, QProcess::ExitStatus) {
            settle(QString::fromUtf8(process->readAll()).trimmed());
        });
        QObject::connect(process, &QProcess::errorOccurred, context, [settle](QProcess::ProcessError) {
            settle({});
        });

        auto *timer = new QTimer(process);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, process, [process]() {
            process->kill();
        });
        timer->start(7000);
        process->start(executable, jobs.at(index).second);
    }
}

struct WifiNetwork
{
    QString ssid;
    int signal = 0;
    bool active = false;
    QString security;
};

QVector<WifiNetwork> wifiNetworks(const QString &output)
{
    QVector<WifiNetwork> networks;
    QSet<QString> seen;

    for (const QString &line : splitLines(output)) {
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

QVector<VpnConnection> vpnConnections(const QString &activeOutput, const QString &allOutput)
{
    QVector<VpnConnection> connections;
    QSet<QString> active;

    for (const QString &line : splitLines(activeOutput)) {
        const QStringList parts = line.split(QLatin1Char(':'));

        if (parts.size() >= 2 &&
            parts.at(1).contains(QStringLiteral("vpn"), Qt::CaseInsensitive)) {
            active.insert(parts.at(0));
        }
    }

    for (const QString &line : splitLines(allOutput)) {
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

WiredInfo wiredInfo(const QString &output)
{
    WiredInfo info;

    for (const QString &line : splitLines(output)) {
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
        painter.setPen(QPen(QColor(QStringLiteral("#00E5FF")), 1.7));
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
        , onColor_(onColor.isValid() ? onColor : QColor(QStringLiteral("#00E5FF")))
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

        const QColor offTrack = enabled_ ? QColor(QStringLiteral("#3A4A5C"))
                                         : QColor(QStringLiteral("#2A3848"));
        const QColor onTrack = enabled_ ? onColor_ : QColor(QStringLiteral("#2A3848"));
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
                "QLabel { color: #8FA3B3; background: transparent; border: none; font-size: 11px; }"));
            row->addWidget(status);
            row->addSpacing(6);
        } else if (active_) {
            auto *check = new QLabel(QStringLiteral("✓"));
            check->setAttribute(Qt::WA_TransparentForMouseEvents);
            check->setStyleSheet(QStringLiteral(
                "QLabel { color: #00E5FF; background: transparent; border: none;"
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
            active_ ? QColor(QStringLiteral("#7AF0FF")) : QColor(QStringLiteral("#2A3848")),
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

        const QColor ink = active_ ? QColor(QStringLiteral("#00E5FF"))
                                   : QColor(QStringLiteral("#C5D4DE"));
        drawKindIcon(painter, kind_, badge, ink);

        const int textLeft = 42;
        const int textRight = width() - 100;
        QFont titleFont = font();
        titleFont.setPointSize(12);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#E7F2F6")));

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
            painter.setPen(QColor(QStringLiteral("#8FA3B3")));
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
    setObjectName(QStringLiteral("edenNetworkMenu"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    QPalette glass = palette();
    glass.setColor(QPalette::Window, Qt::transparent);
    setPalette(glass);
    setStyleSheet(QStringLiteral("#edenNetworkMenu { background: transparent; border: none; }"));
    setFixedWidth(kWidth);
    setMouseTracking(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    QPalette viewPalette = scroll->viewport()->palette();
    viewPalette.setColor(QPalette::Base, Qt::transparent);
    viewPalette.setColor(QPalette::Window, Qt::transparent);
    scroll->viewport()->setPalette(viewPalette);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 8px; background: transparent; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #3A4A5C; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    body_ = new QWidget;
    body_->setAutoFillBackground(false);
    body_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
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

    if (QWidget *popup = QApplication::activePopupWidget()) {
        if (popup->objectName() == QLatin1String("edenNetworkMenu")) {
            popup->close();
            return;
        }
    }

    auto *menu = new NetworkMenu(anchor);
    menu->loadAsync();
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
            QColor(QStringLiteral("#C5D4DE")));
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
        "QLabel { color: #E7F2F6; background: transparent; border: none; }"));
    row->addWidget(title);
    row->addStretch();

    bodyLayout_->insertWidget(bodyLayout_->count() - 1, header);

    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral(
        "QFrame { background: #2A3848; border: none; max-height: 1px; }"));
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
        "QLabel { color: #8FA3B3; background: transparent; border: none;"
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

    if (available && onToggle) {
        card->setToggleCallback([enabled, onToggle]() {
            onToggle(!enabled);
        });
    }

    bodyLayout_->insertWidget(bodyLayout_->count() - 1, card);
}

void NetworkMenu::runAndRefresh(const QString &program, const QStringList &arguments)
{
    runFirstOk({{program, arguments}});
}

void NetworkMenu::runFirstOk(const QVector<QPair<QString, QStringList>> &steps)
{
    if (busy_) {
        return;
    }

    busy_ = true;
    runStep(steps, 0);
}

void NetworkMenu::runStep(const QVector<QPair<QString, QStringList>> &steps, int index)
{
    const auto finished = [this]() {
        busy_ = false;
        loadAsync();
    };

    if (index >= steps.size()) {
        finished();
        return;
    }

    const QString executable = QStandardPaths::findExecutable(steps.at(index).first);

    if (executable.isEmpty()) {
        runStep(steps, index + 1);
        return;
    }

    auto *process = new QProcess(this);
    auto settled = std::make_shared<bool>(false);
    const auto done = [this, process, settled, steps, index](bool ok) {
        if (*settled) {
            return;
        }

        *settled = true;
        process->deleteLater();

        if (ok) {
            busy_ = false;
            loadAsync();
            return;
        }

        runStep(steps, index + 1);
    };
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError) {
        done(false);
    });
    process->start(executable, steps.at(index).second);
}

void NetworkMenu::loadAsync()
{
    const int ticket = ++probeTicket_;

    if (!populated_) {
        showPending();
        placeNearAnchor();
        composePlate();
    }

    const QVector<QPair<QString, QStringList>> jobs = {
        {QStringLiteral("nmcli"), {QStringLiteral("radio"), QStringLiteral("wifi")}},
        {QStringLiteral("nmcli"),
         {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE"), QStringLiteral("device")}},
        {QStringLiteral("bluetoothctl"), {QStringLiteral("list")}},
        {QStringLiteral("bluetoothctl"), {QStringLiteral("show")}},
        {QStringLiteral("nmcli"),
         {QStringLiteral("-t"),
          QStringLiteral("-f"),
          QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"),
          QStringLiteral("device"),
          QStringLiteral("status")}},
        {QStringLiteral("nmcli"),
         {QStringLiteral("-t"),
          QStringLiteral("-f"),
          QStringLiteral("NAME,TYPE"),
          QStringLiteral("connection"),
          QStringLiteral("show"),
          QStringLiteral("--active")}},
        {QStringLiteral("nmcli"),
         {QStringLiteral("-t"),
          QStringLiteral("-f"),
          QStringLiteral("NAME,TYPE"),
          QStringLiteral("connection"),
          QStringLiteral("show")}},
        {QStringLiteral("nmcli"),
         {QStringLiteral("-t"),
          QStringLiteral("-f"),
          QStringLiteral("SSID,SIGNAL,IN-USE,SECURITY"),
          QStringLiteral("device"),
          QStringLiteral("wifi"),
          QStringLiteral("list")}}
    };

    captureMany(this, jobs, [this, ticket](QVector<QString> fields) {
        if (ticket != probeTicket_) {
            return;
        }

        populated_ = true;
        populate(fields);
        placeNearAnchor();
        composePlate();
        update();
    });
}

void NetworkMenu::showPending()
{
    clearRows();
    addHeader();

    auto *loading = new QLabel(QStringLiteral("Carregando conexões…"));
    loading->setStyleSheet(QStringLiteral(
        "QLabel { color: #AEAEB2; background: transparent; border: none;"
        " padding: 8px 6px; }"));
    bodyLayout_->insertWidget(bodyLayout_->count() - 1, loading);
    adjustSize();
}

void NetworkMenu::populate(const QVector<QString> &fields)
{
    const auto field = [&fields](int index) {
        return index >= 0 && index < fields.size() ? fields.at(index) : QString();
    };

    clearRows();
    addHeader();

    const bool hasWifi = wifiHardwarePresent(field(1));
    const bool wifiOn = hasWifi && wifiRadioEnabled(field(0));
    addCard(
        Kind::Wifi,
        QStringLiteral("Wi‑Fi"),
        {},
        wifiOn,
        hasWifi,
        wifiOn,
        QColor(QStringLiteral("#00E5FF")),
        [this](bool enable) {
            runAndRefresh(
                QStringLiteral("nmcli"),
                {QStringLiteral("radio"),
                 QStringLiteral("wifi"),
                 enable ? QStringLiteral("on") : QStringLiteral("off")});
        });

    const bool hasBluetooth = bluetoothAvailable(field(2));
    const bool bluetoothOn = hasBluetooth && bluetoothPowered(field(3));
    addCard(
        Kind::Bluetooth,
        QStringLiteral("Bluetooth"),
        {},
        bluetoothOn,
        hasBluetooth,
        bluetoothOn,
        QColor(QStringLiteral("#00E5FF")),
        [this](bool enable) {
            runAndRefresh(
                QStringLiteral("bluetoothctl"),
                {QStringLiteral("power"),
                 enable ? QStringLiteral("on") : QStringLiteral("off")});
        });

    const WiredInfo wired = wiredInfo(field(4));

    if (wired.present) {
        addCard(
            Kind::Ethernet,
            QStringLiteral("Ethernet"),
            wired.detail,
            wired.connected,
            true,
            wired.connected,
            QColor(QStringLiteral("#00E5FF")),
            [this, wired](bool enable) {
                if (wired.device.isEmpty()) {
                    return;
                }

                QVector<QPair<QString, QStringList>> steps;

                if (enable) {
                    if (!wired.connection.isEmpty()) {
                        steps.push_back(
                            {QStringLiteral("nmcli"),
                             {QStringLiteral("connection"), QStringLiteral("up"), wired.connection}});
                    }

                    steps.push_back(
                        {QStringLiteral("nmcli"),
                         {QStringLiteral("device"), QStringLiteral("connect"), wired.device}});
                } else {
                    steps.push_back(
                        {QStringLiteral("nmcli"),
                         {QStringLiteral("device"), QStringLiteral("disconnect"), wired.device}});

                    if (!wired.connection.isEmpty()) {
                        steps.push_back(
                            {QStringLiteral("nmcli"),
                             {QStringLiteral("connection"), QStringLiteral("down"), wired.connection}});
                    }
                }

                runFirstOk(steps);
            });
    }

    const QVector<VpnConnection> vpns = vpnConnections(field(5), field(6));
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
        const QVector<WifiNetwork> networks = wifiNetworks(field(7));

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
                    QColor(QStringLiteral("#00E5FF")),
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
    applyShape();
}

void NetworkMenu::applyShape()
{
    if (width() < 8 || height() < 8) {
        return;
    }

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void NetworkMenu::composePlate()
{
    if (width() < 8 || height() < 8) {
        return;
    }

    if (!isVisible()) {
        QScreen *screen = this->screen();

        if (screen == nullptr && anchor_ != nullptr) {
            screen = anchor_->screen();
        }

        if (screen != nullptr) {
            backdrop_ = screen->grabWindow(0, x(), y(), width(), height());
        }
    }

    const QPixmap frosted = blurredBackdrop(backdrop_);

    constexpr int scale = 3;
    QImage sharp(width() * scale, height() * scale, QImage::Format_ARGB32_Premultiplied);
    sharp.fill(Qt::transparent);

    QPainter painter(&sharp);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);

    if (!frosted.isNull()) {
        painter.drawPixmap(rect(), frosted, frosted.rect());
    } else if (!backdrop_.isNull()) {
        painter.drawPixmap(rect(), backdrop_, backdrop_.rect());
    } else {
        painter.fillRect(rect(), QColor(18, 24, 32));
    }

    const QRectF box(0.75, 0.75, width() - 1.5, height() - 1.5);
    QPainterPath shape;
    shape.addRoundedRect(box, kRadius, kRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 24, 32, 217));
    painter.drawPath(shape);
    painter.end();

    plate_ = QPixmap::fromImage(sharp.scaled(
        size(),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation));
    applyShape();
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
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 24, 32, 230));
    painter.drawPath(shape);
}

void NetworkMenu::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyShape();
}

void NetworkMenu::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyShape();
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
