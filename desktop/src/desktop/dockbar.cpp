#include "desktop/dockbar.hpp"

#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QImage>

#include <cairo/cairo.h>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <cstring>
#include <cstdlib>
#include <iterator>

extern "C" {

struct RsvgHandle;
struct RsvgRectangle
{
    double x;
    double y;
    double width;
    double height;
};

RsvgHandle *rsvg_handle_new_from_file(const char *filename, void **error);
int rsvg_handle_render_document(
    RsvgHandle *handle,
    cairo_t *context,
    const RsvgRectangle *viewport,
    void **error);
void g_object_unref(void *object);

}

namespace {

QString iconFile(const QString &name)
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath("../assets/icons/" + name),
        binaryDirectory.filePath("assets/icons/" + name),
        QStringLiteral("assets/icons/") + name
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }

    return {};
}

QPixmap loadIcon(const QString &name)
{
    const QString path = iconFile(name);

    if (path.isEmpty()) {
        return {};
    }

    const QByteArray encoded = QFile::encodeName(path);
    RsvgHandle *handle = rsvg_handle_new_from_file(encoded.constData(), nullptr);

    if (handle == nullptr) {
        return {};
    }

    constexpr int size = 64;
    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *context = cairo_create(surface);
    const RsvgRectangle viewport{0, 0, static_cast<double>(size), static_cast<double>(size)};
    const int rendered = rsvg_handle_render_document(handle, context, &viewport, nullptr);
    cairo_destroy(context);
    g_object_unref(handle);

    if (rendered == 0) {
        cairo_surface_destroy(surface);
        return {};
    }

    cairo_surface_flush(surface);
    const QImage image(
        cairo_image_surface_get_data(surface),
        size,
        size,
        cairo_image_surface_get_stride(surface),
        QImage::Format_ARGB32_Premultiplied);
    const QPixmap pixmap = QPixmap::fromImage(image.copy());
    cairo_surface_destroy(surface);
    return pixmap;
}

bool launchExecutable(const QString &path)
{
    return !path.isEmpty() && QFileInfo::exists(path) &&
           QProcess::startDetached(path, {});
}

bool launchNamed(const QStringList &names)
{
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);

        if (launchExecutable(path)) {
            return true;
        }
    }

    return false;
}

bool launchFiles()
{
    const QString edens =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("edens"));

    return launchExecutable(edens);
}

bool launchTerminal()
{
    return launchNamed({
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("mate-terminal"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("xterm")
    });
}

bool launchIde()
{
    return launchNamed({
        QStringLiteral("code"),
        QStringLiteral("codium"),
        QStringLiteral("gnome-text-editor"),
        QStringLiteral("gedit"),
        QStringLiteral("xed"),
        QStringLiteral("mousepad"),
        QStringLiteral("kate")
    });
}

bool launchGit()
{
    return launchNamed({
        QStringLiteral("git-gui"),
        QStringLiteral("gitk")
    });
}

bool launchDocker()
{
    return launchNamed({
        QStringLiteral("docker")
    });
}

bool launchDatabase()
{
    return launchNamed({
        QStringLiteral("dbeaver"),
        QStringLiteral("mysql-workbench")
    });
}

bool launchApi()
{
    return launchNamed({
        QStringLiteral("insomnia"),
        QStringLiteral("postman")
    });
}

bool launchPackage()
{
    return launchNamed({
        QStringLiteral("synaptic"),
        QStringLiteral("gnome-software")
    });
}

bool launchDebugger()
{
    return launchNamed({
        QStringLiteral("gdbgui")
    });
}

bool launchSettings()
{
    return false;
}

QPixmap spacesIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(201, 212, 245));
    painter.drawRoundedRect(2, 2, 60, 60, 16, 16);
    painter.setBrush(QColor(30, 30, 46));

    const QRect cells[] = {
        QRect(14, 14, 15, 15),
        QRect(35, 14, 15, 15),
        QRect(14, 35, 15, 15),
        QRect(35, 35, 15, 15)
    };

    for (const QRect &cell : cells) {
        painter.drawRoundedRect(cell, 4, 4);
    }

    return pixmap;
}

xcb_connection_t *x11Connection()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

    if (native == nullptr) {
        return nullptr;
    }

    return native->connection();
}

xcb_atom_t internAtom(xcb_connection_t *connection, const char *name)
{
    const auto cookie = xcb_intern_atom(
        connection,
        0,
        static_cast<uint16_t>(std::strlen(name)),
        name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }

    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

uint32_t currentDesktop()
{
    xcb_connection_t *connection = x11Connection();

    if (connection == nullptr) {
        return 0;
    }

    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;

    if (screen == nullptr) {
        return 0;
    }

    const xcb_atom_t atom = internAtom(connection, "_NET_CURRENT_DESKTOP");

    if (atom == XCB_ATOM_NONE) {
        return 0;
    }

    const auto cookie = xcb_get_property(
        connection,
        0,
        screen->root,
        atom,
        XCB_ATOM_CARDINAL,
        0,
        1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);

    if (reply == nullptr || xcb_get_property_value_length(reply) < 4) {
        std::free(reply);
        return 0;
    }

    const auto desktop = *static_cast<const uint32_t *>(xcb_get_property_value(reply));
    std::free(reply);
    return desktop;
}

void requestDesktop(uint32_t desktop)
{
    xcb_connection_t *connection = x11Connection();

    if (connection == nullptr) {
        return;
    }

    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;

    if (screen == nullptr) {
        return;
    }

    const xcb_atom_t atom = internAtom(connection, "_NET_CURRENT_DESKTOP");

    if (atom == XCB_ATOM_NONE) {
        return;
    }

    xcb_client_message_event_t event = {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = screen->root;
    event.type = atom;
    event.data.data32[0] = desktop;
    event.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(
        connection,
        0,
        screen->root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<const char *>(&event));
    xcb_flush(connection);
}

} // namespace

DockBar::DockBar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(
        Qt::Tool |
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::BypassWindowManagerHint |
        Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_X11DoNotAcceptFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    buildEntries();

    auto *keepOnTop = new QTimer(this);
    connect(keepOnTop, &QTimer::timeout, this, [this]() {
        raise();
    });
    keepOnTop->start(400);
}

void DockBar::buildEntries()
{
    const struct {
        const char *label;
        const char *icon;
        bool separator;
        bool (*launch)();
    } specs[] = {
        {"Arquivos", "eden-launcher.svg", false, launchFiles},
        {"Terminal", "eden-terminal.svg", false, launchTerminal},
        {"Editor", "eden-ide.svg", false, launchIde},
        {"Git", "eden-git.svg", false, launchGit},
        {"Docker", "eden-docker.svg", false, launchDocker},
        {"Banco", "eden-database.svg", false, launchDatabase},
        {"API", "eden-api.svg", false, launchApi},
        {"Pacotes", "eden-package.svg", false, launchPackage},
        {"Depurador", "eden-debugger.svg", false, launchDebugger},
        {"", "", true, nullptr},
        {"Áreas", "", false, nullptr},
        {"Ajustes", "eden-settings.svg", false, launchSettings}
    };

    entries_.reserve(static_cast<int>(std::size(specs)));

    for (const auto &spec : specs) {
        Entry entry;
        entry.label = QString::fromUtf8(spec.label);
        entry.separator = spec.separator;
        entry.launch = spec.launch;

        if (entry.label == QStringLiteral("Áreas")) {
            entry.desktopMenu = true;
            entry.icon = spacesIcon();
        } else if (!entry.separator) {
            entry.icon = loadIcon(QString::fromUtf8(spec.icon));
        }

        entries_.push_back(entry);
    }
}

void DockBar::place(const QRect &screen)
{
    screen_ = screen;
    relayout();
}

int DockBar::iconSize(int index) const
{
    if (index < 0 || index >= entries_.size() || entries_.at(index).separator) {
        return 0;
    }

    return index == hover_ ? kIconHover : kIconRest;
}

void DockBar::relayout()
{
    int contentWidth = kPadX;

    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).separator) {
            contentWidth += 18;
            continue;
        }

        contentWidth += kIconHover + kGap;
    }

    contentWidth += kPadX - kGap;

    const int barHeight = kPadY + kIconHover + 8 + kPadY;
    const int totalHeight = kLabelHeight + kLabelGap + barHeight;
    const int x = screen_.x() + qMax(0, (screen_.width() - contentWidth) / 2);
    const int y = screen_.y() + screen_.height() - kBottomMargin - totalHeight;

    const QRect nextGeometry(x, y, qMax(contentWidth, 1), totalHeight);

    if (geometry() != nextGeometry) {
        setGeometry(nextGeometry);
    }
    barRect_ = QRect(0, kLabelHeight + kLabelGap, width(), barHeight);

    iconRects_.resize(entries_.size());
    int cursor = kPadX;

    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).separator) {
            iconRects_[index] = QRect(cursor, barRect_.top(), 18, barRect_.height());
            cursor += 18;
            continue;
        }

        const int slot = kIconHover;
        const int size = iconSize(index);
        const int iconX = cursor + (slot - size) / 2;
        const int iconY = barRect_.bottom() - kPadY - 8 - size;
        iconRects_[index] = QRect(iconX, iconY, size, size);
        cursor += slot + kGap;
    }

    labelRect_ = QRect();

    if (hover_ >= 0 && hover_ < entries_.size() && !entries_.at(hover_).separator) {
        const QFontMetrics metrics(font());
        const int textWidth = metrics.horizontalAdvance(entries_.at(hover_).label) + 18;
        const QRect icon = iconRects_.at(hover_);
        const int labelX = qBound(4, icon.center().x() - textWidth / 2, width() - textWidth - 4);
        labelRect_ = QRect(labelX, 0, textWidth, kLabelHeight);
    }

    updateMask();
    update();
}

void DockBar::updateMask()
{
    QBitmap mask(size());
    mask.fill(Qt::color0);

    QPainter painter(&mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::color1);

    QPainterPath path;
    path.addRoundedRect(barRect_.adjusted(0, 0, -1, -1), 22, 22);

    if (!labelRect_.isNull()) {
        path.addRoundedRect(labelRect_.adjusted(0, 0, -1, -1), 8, 8);
    }

    painter.drawPath(path);
    setMask(mask);
}

int DockBar::entryAt(const QPoint &position) const
{
    int nearest = -1;
    int nearestDistance = 0;

    for (int index = 0; index < iconRects_.size(); ++index) {
        if (entries_.at(index).separator) {
            continue;
        }

        const int extra = (kIconHover - iconRects_.at(index).width()) / 2 + kGap / 2;
        const QRect hit = iconRects_.at(index).adjusted(-extra, -24, extra, 18);

        if (!hit.contains(position)) {
            continue;
        }

        const int distance = std::abs(position.x() - hit.center().x());

        if (nearest < 0 || distance < nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }

    return nearest;
}

void DockBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setPen(Qt::NoPen);

    painter.setBrush(QColor(36, 36, 40));
    painter.drawRoundedRect(barRect_.adjusted(0, 0, -1, -1), 22, 22);
    painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(barRect_.adjusted(1, 1, -2, -2), 21, 21);

    for (int index = 0; index < entries_.size(); ++index) {
        const Entry &entry = entries_.at(index);
        const QRect rect = iconRects_.at(index);

        if (entry.separator) {
            painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
            const int lineX = rect.center().x();
            painter.drawLine(lineX, barRect_.top() + 16, lineX, barRect_.bottom() - 16);
            continue;
        }

        painter.setPen(Qt::NoPen);

        if (!entry.icon.isNull()) {
            painter.drawPixmap(rect, entry.icon);
        } else {
            painter.setBrush(QColor(80, 110, 160));
            painter.drawRoundedRect(rect, 12, 12);
        }

        if (entry.launched) {
            painter.setBrush(QColor(245, 245, 247));
            painter.drawEllipse(QPoint(rect.center().x(), barRect_.bottom() - 8), 2, 2);
        }
    }

    if (labelRect_.isNull()) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(246, 246, 248));
    painter.drawRoundedRect(labelRect_, 8, 8);
    painter.setPen(QColor(28, 28, 30));
    painter.drawText(labelRect_, Qt::AlignCenter, entries_.at(hover_).label);
}

void DockBar::mouseMoveEvent(QMouseEvent *event)
{
    const int next = entryAt(event->position().toPoint());

    if (next == hover_) {
        return;
    }

    hover_ = next;
    relayout();
}

void DockBar::leaveEvent(QEvent *)
{
    if (hover_ < 0) {
        return;
    }

    hover_ = -1;
    relayout();
}

void DockBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const int index = entryAt(event->position().toPoint());

    if (index < 0) {
        return;
    }

    if (entries_.at(index).desktopMenu || entries_.at(index).launch == nullptr) {
        return;
    }

    if (entries_[index].launch()) {
        entries_[index].launched = true;
        update();
    }
}

void DockBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const int index = entryAt(event->position().toPoint());

    if (index < 0 || !entries_.at(index).desktopMenu) {
        return;
    }

    showDesktopMenu(index);
}

void DockBar::showDesktopMenu(int index)
{
    const uint32_t active = currentDesktop();
    QMenu menu;
    menu.setStyleSheet(QStringLiteral(R"(
        QMenu {
            background: #F5F5F7;
            color: #1C1C1E;
            border: 1px solid #D2D2D7;
            border-radius: 12px;
            padding: 6px;
        }

        QMenu::item {
            padding: 7px 22px 7px 16px;
            border-radius: 7px;
            background: transparent;
        }

        QMenu::item:selected {
            background: #0A84FF;
            color: #FFFFFF;
        }
    )"));

    QAction *actions[4] = {};

    for (int desktop = 0; desktop < 4; ++desktop) {
        const QString mark = static_cast<uint32_t>(desktop) == active
            ? QString::fromUtf8("✓   ")
            : QStringLiteral("     ");
        actions[desktop] = menu.addAction(
            mark + QStringLiteral("Área de trabalho %1").arg(desktop + 1));
    }

    menu.adjustSize();
    const QRect icon = iconRects_.value(index);
    const QSize hint = menu.sizeHint();
    QPoint popup(
        icon.center().x() - hint.width() / 2,
        barRect_.top() - hint.height() - 10);
    popup = mapToGlobal(popup);
    popup.setX(qBound(screen_.x() + 8, popup.x(), screen_.right() - hint.width() - 8));

    QTimer::singleShot(0, &menu, [&menu]() {
        menu.raise();
        menu.repaint();
    });

    QAction *chosen = menu.exec(popup);

    for (int desktop = 0; desktop < 4; ++desktop) {
        if (chosen == actions[desktop]) {
            requestDesktop(static_cast<uint32_t>(desktop));
            entries_[index].launched = true;
            update();
            return;
        }
    }
}
