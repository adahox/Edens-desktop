#include "desktop/screenshot.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <functional>

namespace {

constexpr uint8_t kKeyPrint = 107;

QRect virtualDesktopGeometry()
{
    QRect geometry;

    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen == nullptr) {
            continue;
        }

        geometry = geometry.united(screen->geometry());
    }

    if (!geometry.isEmpty()) {
        return geometry;
    }

    QScreen *primary = QGuiApplication::primaryScreen();
    return primary == nullptr ? QRect() : primary->virtualGeometry();
}

QPixmap grabVirtualDesktop()
{
    const QRect bounds = virtualDesktopGeometry();

    if (bounds.isEmpty()) {
        return {};
    }

    QPixmap canvas(bounds.size());
    canvas.fill(Qt::black);

    QPainter painter(&canvas);

    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen == nullptr) {
            continue;
        }

        const QRect geometry = screen->geometry();
        const QPixmap piece = screen->grabWindow(0);

        if (piece.isNull()) {
            continue;
        }

        painter.drawPixmap(geometry.topLeft() - bounds.topLeft(), piece);
    }

    painter.end();
    return canvas;
}

void showToast(const QString &text)
{
    auto *toast = new QLabel(text, nullptr);
    toast->setWindowFlags(
        Qt::Tool |
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::BypassWindowManagerHint);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: rgba(20, 24, 34, 220);"
        "  color: #F2F4F8;"
        "  border: 1px solid #3A455C;"
        "  border-radius: 12px;"
        "  padding: 12px 18px;"
        "  font-size: 13px;"
        "}"));
    toast->adjustSize();

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        toast->move(
            area.center().x() - toast->width() / 2,
            area.top() + 48);
    }

    toast->show();
    QTimer::singleShot(2200, toast, &QWidget::close);
}

class RegionCaptureOverlay final : public QWidget
{
public:
    using FinishCallback = std::function<void(const QPixmap &)>;

    RegionCaptureOverlay(const QPixmap &source, const QRect &bounds, FinishCallback finish)
        : QWidget(nullptr)
        , source_(source)
        , bounds_(bounds)
        , finish_(std::move(finish))
    {
        setWindowFlags(
            Qt::Tool |
            Qt::FramelessWindowHint |
            Qt::WindowStaysOnTopHint |
            Qt::BypassWindowManagerHint |
            Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        setGeometry(bounds_);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        raise();
        activateWindow();
        setFocus();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        if (!source_.isNull()) {
            painter.drawPixmap(rect(), source_, source_.rect());
        } else {
            painter.fillRect(rect(), QColor(20, 24, 34));
        }

        const QRect selected = normalizedSelection();

        if (selected.isValid() && selected.width() > 0 && selected.height() > 0) {
            QPainterPath shade;
            shade.addRect(QRectF(rect()));
            QPainterPath hole;
            hole.addRect(QRectF(selected));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 140));
            painter.drawPath(shade.subtracted(hole));

            if (!source_.isNull()) {
                painter.drawPixmap(selected, source_, selected);
            }

            painter.setPen(QPen(QColor(QStringLiteral("#0A84FF")), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(selected.adjusted(0, 0, -1, -1));

            const QString sizeText = QStringLiteral("%1 × %2")
                                         .arg(selected.width())
                                         .arg(selected.height());
            QFont font = painter.font();
            font.setPointSize(11);
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QFontMetrics metrics(font);
            const QSize labelSize = metrics.size(0, sizeText) + QSize(14, 8);
            QPoint labelPos(selected.left(), selected.top() - labelSize.height() - 6);

            if (labelPos.y() < 8) {
                labelPos.setY(selected.top() + 8);
            }

            if (labelPos.x() + labelSize.width() > width() - 8) {
                labelPos.setX(std::max(8, width() - labelSize.width() - 8));
            }

            const QRect labelRect(labelPos, labelSize);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(20, 24, 34, 210));
            painter.drawRoundedRect(labelRect, 8, 8);
            painter.setPen(Qt::white);
            painter.drawText(labelRect, Qt::AlignCenter, sizeText);
        } else {
            painter.fillRect(rect(), QColor(0, 0, 0, 140));

            if (!dragging_) {
                QFont font = painter.font();
                font.setPointSize(14);
                font.setWeight(QFont::Medium);
                painter.setFont(font);
                painter.setPen(QColor(255, 255, 255, 230));
                painter.drawText(
                    rect(),
                    Qt::AlignCenter,
                    QStringLiteral("Arraste para selecionar a área · Esc cancela"));
            }
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }

        dragging_ = true;
        origin_ = event->position().toPoint();
        current_ = origin_;
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_) {
            return;
        }

        current_ = event->position().toPoint();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !dragging_) {
            return;
        }

        dragging_ = false;
        current_ = event->position().toPoint();
        const QRect selected = normalizedSelection();

        if (selected.width() < 4 || selected.height() < 4) {
            update();
            return;
        }

        const QPixmap cropped = source_.copy(selected);
        hide();

        if (finish_) {
            finish_(cropped);
        }

        close();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }

        QWidget::keyPressEvent(event);
    }

private:
    [[nodiscard]] QRect normalizedSelection() const
    {
        if (!dragging_ && origin_ == current_) {
            return {};
        }

        return QRect(origin_, current_).normalized().intersected(rect());
    }

    QPixmap source_;
    QRect bounds_;
    FinishCallback finish_;
    QPoint origin_;
    QPoint current_;
    bool dragging_ = false;
};

}  // namespace

ScreenshotService::ScreenshotService()
    : QObject(qApp)
{
}

void ScreenshotService::install()
{
    auto *service = new ScreenshotService;
    qApp->installNativeEventFilter(service);
    service->grabHotkey();
}

bool ScreenshotService::nativeEventFilter(
    const QByteArray &eventType,
    void *message,
    qintptr *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || message == nullptr) {
        return false;
    }

    const auto *event = static_cast<const xcb_generic_event_t *>(message);
    const uint8_t type = event->response_type & 0x7f;
    const auto *key = reinterpret_cast<const xcb_key_press_event_t *>(event);

    if (key->detail != kKeyPrint) {
        return false;
    }

    if (type == XCB_KEY_RELEASE) {
        lastRelease_ = key->time;
        haveRelease_ = true;
        return false;
    }

    if (type != XCB_KEY_PRESS) {
        return false;
    }

    if (haveRelease_ && key->time - lastRelease_ < 40) {
        return true;
    }

    const bool control = (key->state & XCB_MOD_MASK_CONTROL) != 0;

    QTimer::singleShot(0, this, [this, control]() {
        if (control) {
            captureRegion();
        } else {
            captureFull();
        }
    });
    return true;
}

void ScreenshotService::grabHotkey()
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
            lock,
            kKeyPrint,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC);
        xcb_grab_key(
            connection,
            1,
            screen->root,
            static_cast<uint16_t>(XCB_MOD_MASK_CONTROL | lock),
            kKeyPrint,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC);
    }

    xcb_flush(connection);
}

void ScreenshotService::saveShot(const QPixmap &shot)
{
    if (shot.isNull()) {
        showToast(QStringLiteral("Falha ao capturar a tela."));
        return;
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setPixmap(shot);
    }

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString folder = QDir(pictures).filePath(QStringLiteral("Screenshots"));
    QDir().mkpath(folder);

    const QString fileName = QStringLiteral("eden-%1.png").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QDir(folder).filePath(fileName);

    if (!shot.save(path, "PNG")) {
        showToast(QStringLiteral("Captura na área de transferência (falha ao salvar)."));
        return;
    }

    showToast(QStringLiteral("Print salvo em Imagens/Screenshots"));
}

void ScreenshotService::copyShot(const QPixmap &shot)
{
    if (shot.isNull()) {
        showToast(QStringLiteral("Falha ao capturar a tela."));
        return;
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setPixmap(shot);
    }

    showToast(QStringLiteral("Captura copiada — Ctrl+V no desktop para salvar"));
}

void ScreenshotService::captureFull()
{
    const QPixmap shot = grabVirtualDesktop();
    saveShot(shot);
}

void ScreenshotService::captureRegion()
{
    const QRect bounds = virtualDesktopGeometry();
    const QPixmap source = grabVirtualDesktop();

    if (bounds.isEmpty() || source.isNull()) {
        showToast(QStringLiteral("Não foi possível capturar a tela."));
        return;
    }

    auto *overlay = new RegionCaptureOverlay(
        source,
        bounds,
        [this](const QPixmap &shot) {
            copyShot(shot);
        });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
}
