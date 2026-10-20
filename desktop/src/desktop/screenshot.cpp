#include "desktop/screenshot.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QLabel>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

namespace {

constexpr uint8_t kKeyPrint = 107;

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

    QTimer::singleShot(0, this, [this]() {
        capture();
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
    }

    xcb_flush(connection);
}

void ScreenshotService::capture()
{
    QScreen *screen = QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        showToast(QStringLiteral("Não foi possível capturar a tela."));
        return;
    }

    // Small delay so modifier/key UI settles; Print itself has no overlay.
    const QPixmap shot = screen->grabWindow(0);

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
