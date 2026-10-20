#include "desktop/dockbar.hpp"
#include "desktop/desktopwindow.hpp"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QBitmap>
#include <QByteArray>
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
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QImage>
#include <QLinearGradient>
#include <QShowEvent>

#include <cmath>

#include <cairo/cairo.h>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <csignal>
#include <cstring>
#include <cstdlib>
#include <iterator>

#include <spawn.h>
#include <unistd.h>

extern char **environ;

DockBar *DockBar::active_ = nullptr;

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

QString wallpaperFile()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath(QStringLiteral("../assets/wallpapers/default.jpg")),
        binaryDirectory.filePath(QStringLiteral("assets/wallpapers/default.jpg")),
        QStringLiteral("assets/wallpapers/default.jpg")
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }

    return {};
}

void makeOpaque(QImage &image)
{
    image = image.convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));

        for (int x = 0; x < image.width(); ++x) {
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
        }
    }
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

QPixmap buildIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(166, 227, 161));
    painter.drawRoundedRect(2, 2, 60, 60, 16, 16);
    painter.setPen(QPen(QColor(17, 17, 27), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(18, 34), QPointF(28, 46));
    painter.drawLine(QPointF(28, 46), QPointF(48, 22));
    return pixmap;
}

bool sameDisplay(qint64 pid)
{
    QFile environment(QStringLiteral("/proc/%1/environ").arg(pid));

    if (!environment.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QString display = qEnvironmentVariable("DISPLAY");
    const QStringList variables = QString::fromUtf8(environment.readAll()).split(QChar('\0'));

    return variables.contains(QStringLiteral("DISPLAY=") + display);
}

bool launchAfterDelay(const QString &program, const char *delaySeconds)
{
    const QByteArray shell("/bin/sh");
    const QByteArray script = QByteArray("sleep ") + delaySeconds + "; exec \"$1\"";
    const QByteArray name("sh");
    const QByteArray target = QFile::encodeName(program);
    char *argv[] = {
        const_cast<char *>(shell.constData()),
        const_cast<char *>("-c"),
        const_cast<char *>(script.constData()),
        const_cast<char *>(name.constData()),
        const_cast<char *>(target.constData()),
        nullptr
    };

    posix_spawnattr_t attributes;

    if (posix_spawnattr_init(&attributes) != 0) {
        return false;
    }

    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
    pid_t pid = 0;
    const int result = posix_spawn(&pid, shell.constData(), nullptr, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    return result == 0 && pid > 0;
}

void stopDesktopTools()
{
    QDir processes(QStringLiteral("/proc"));
    const QStringList entries = processes.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.toLongLong(&numeric);

        if (!numeric || pid == QCoreApplication::applicationPid() || !sameDisplay(pid)) {
            continue;
        }

        QFile command(QStringLiteral("/proc/%1/comm").arg(entry));

        if (!command.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QString name = QString::fromUtf8(command.readAll()).trimmed();

        if (name == QStringLiteral("eden-desktop") || name.startsWith(QStringLiteral("windowmanager"))) {
            kill(static_cast<pid_t>(pid), SIGTERM);
        }
    }
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
        if (QApplication::activePopupWidget() != nullptr ||
            QApplication::activeModalWidget() != nullptr) {
            return;
        }

        raise();
    });
    keepOnTop->start(400);

    loadingTimer_ = new QTimer(this);
    connect(loadingTimer_, &QTimer::timeout, this, [this]() {
        tickLoading();
    });
}

void DockBar::bind(DockBar *dock)
{
    active_ = dock;
}

DockBar *DockBar::instance()
{
    return active_;
}

int DockBar::findEntry(const QString &label) const
{
    for (int index = 0; index < entries_.size(); ++index) {
        if (!entries_.at(index).separator &&
            entries_.at(index).label.compare(label, Qt::CaseInsensitive) == 0) {
            return index;
        }
    }

    return -1;
}

int DockBar::separatorIndex() const
{
    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).separator) {
            return index;
        }
    }

    return entries_.size();
}

void DockBar::beginLoading(int index)
{
    if (index < 0 || index >= entries_.size() || entries_.at(index).separator) {
        return;
    }

    entries_[index].loading = true;
    entries_[index].launched = false;
    entries_[index].loadingUntil =
        QDateTime::currentMSecsSinceEpoch() + kLoadingMs;

    if (!loadingTimer_->isActive()) {
        loadingPhase_ = 0.0;
        loadingTimer_->start(16);
    }

    relayout();
}

void DockBar::notifyOpening(const QString &label, const QIcon &icon)
{
    if (label.trimmed().isEmpty()) {
        return;
    }

    const int existing = findEntry(label);

    if (existing >= 0) {
        beginLoading(existing);
        return;
    }

    Entry entry;
    entry.label = label.trimmed();
    entry.ephemeral = true;
    entry.icon = icon.pixmap(kIconHover, kIconHover);

    if (entry.icon.isNull()) {
        entry.icon = QPixmap(kIconHover, kIconHover);
        entry.icon.fill(QColor(80, 110, 160));
    }

    const int insertAt = separatorIndex();
    entries_.insert(insertAt, entry);
    beginLoading(insertAt);
}

void DockBar::tickLoading()
{
    loadingPhase_ += 0.22;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyLoading = false;

    for (int index = 0; index < entries_.size(); ++index) {
        Entry &entry = entries_[index];

        if (!entry.loading) {
            continue;
        }

        anyLoading = true;

        if (now >= entry.loadingUntil) {
            entry.loading = false;
            entry.launched = true;
        }
    }

    if (!anyLoading) {
        loadingTimer_->stop();
    }

    relayout();
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
        {"Ajustes", "eden-settings.svg", false, launchSettings},
        {"Compilar", "", false, nullptr}
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
        } else if (entry.label == QStringLiteral("Compilar")) {
            entry.buildsDesktop = true;
            entry.icon = buildIcon();
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

    int size = index == hover_ ? kIconHover : kIconRest;

    if (entries_.at(index).loading) {
        size += static_cast<int>(
            3.0 * std::abs(std::sin(loadingPhase_ * 1.6 + index * 0.7)));
    }

    return size;
}

int DockBar::bounceOffset(int index) const
{
    if (index < 0 || index >= entries_.size() || !entries_.at(index).loading) {
        return 0;
    }

    return static_cast<int>(
        -10.0 * std::abs(std::sin(loadingPhase_ * 2.4 + index * 0.55)));
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
        const int iconY = barRect_.bottom() - kPadY - 8 - size + bounceOffset(index);
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

    if (glass_.size() != barRect_.size()) {
        refreshGlass();
    }

    compose();
    update();
}

void DockBar::compose()
{
    if (width() <= 0 || height() <= 0 || barRect_.isEmpty()) {
        composed_ = QPixmap();
        return;
    }

    constexpr int scale = 3;
    QImage hi(width() * scale, height() * scale, QImage::Format_ARGB32_Premultiplied);
    hi.fill(Qt::transparent);

    QPainter painter(&hi);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);

    if (!backdrop_.isNull()) {
        painter.drawPixmap(0, 0, backdrop_);
    } else {
        painter.fillRect(rect(), QColor(32, 32, 40));
    }

    QPainterPath capsule;
    capsule.addRoundedRect(barRect_.adjusted(0, 0, -1, -1), 22, 22);
    painter.setClipPath(capsule);

    if (!glass_.isNull()) {
        painter.drawPixmap(barRect_.topLeft(), glass_);
    }

    painter.end();

    composed_ = QPixmap::fromImage(hi.scaled(
        size(),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation));
}

void DockBar::showEvent(QShowEvent *)
{
    refreshGlass();
    compose();
    update();
}

void DockBar::refreshGlass()
{
    glass_ = QPixmap();

    if (barRect_.isEmpty() || screen_.isEmpty()) {
        return;
    }

    const QString path = wallpaperFile();
    const QPixmap source(path);
    QImage plate(barRect_.size(), QImage::Format_ARGB32_Premultiplied);

    if (source.isNull()) {
        plate.fill(QColor(236, 236, 240));
    } else {
        const QPixmap covered = source.scaled(
            screen_.size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const int originX = qMax(0, (covered.width() - screen_.width()) / 2);
        const int originY = qMax(0, (covered.height() - screen_.height()) / 2);
        const QPoint globalBar = mapToGlobal(barRect_.topLeft());
        const QRect sample(
            originX + globalBar.x() - screen_.x(),
            originY + globalBar.y() - screen_.y(),
            barRect_.width(),
            barRect_.height());
        QImage piece = covered.copy(sample).toImage();

        if (piece.isNull()) {
            plate.fill(QColor(236, 236, 240));
        } else {
            makeOpaque(piece);
            const QImage small = piece.scaled(
                qMax(1, piece.width() / 12),
                qMax(1, piece.height() / 12),
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
            plate = small.scaled(piece.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            makeOpaque(plate);
        }
    }

    QPainter frost(&plate);
    frost.fillRect(plate.rect(), QColor(255, 255, 255, 148));

    QLinearGradient sheen(0, 0, 0, plate.height());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 150));
    sheen.setColorAt(0.38, QColor(255, 255, 255, 28));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 70));
    frost.fillRect(plate.rect(), sheen);
    frost.end();
    makeOpaque(plate);

    glass_ = QPixmap::fromImage(plate);

    backdrop_ = QPixmap(size());
    backdrop_.fill(QColor(32, 32, 40));

    if (source.isNull()) {
        return;
    }

    const QPixmap covered = source.scaled(
        screen_.size(),
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);
    const int originX = qMax(0, (covered.width() - screen_.width()) / 2);
    const int originY = qMax(0, (covered.height() - screen_.height()) / 2);
    const QPoint globalTopLeft = mapToGlobal(QPoint(0, 0));
    const QRect widgetSample(
        originX + globalTopLeft.x() - screen_.x(),
        originY + globalTopLeft.y() - screen_.y(),
        width(),
        height());
    const QPixmap slice = covered.copy(widgetSample);

    if (!slice.isNull()) {
        backdrop_ = slice;
    }
}

void DockBar::updateMask()
{
    QRegion region(barRect_.adjusted(0, -2, 0, 2));

    if (!labelRect_.isNull()) {
        region = region.united(QRegion(labelRect_.adjusted(-3, -3, 3, 3)));
    }

    setMask(region);
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

    if (!composed_.isNull()) {
        painter.drawPixmap(0, 0, composed_);
    } else if (!glass_.isNull()) {
        painter.drawPixmap(barRect_.topLeft(), glass_);
    }

    for (int index = 0; index < entries_.size(); ++index) {
        const Entry &entry = entries_.at(index);
        const QRect rect = iconRects_.at(index);

        if (entry.separator) {
            painter.setPen(QPen(QColor(255, 255, 255, 150), 1));
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

        if (entry.loading) {
            QPainterPath clip;
            clip.addRoundedRect(rect, 12, 12);
            painter.save();
            painter.setClipPath(clip);
            painter.fillRect(rect, QColor(0, 0, 0, 90));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 230), 2.4, Qt::SolidLine, Qt::RoundCap));
            const QRect arc = rect.adjusted(10, 10, -10, -10);
            const int start = static_cast<int>(-loadingPhase_ * 280) * 16;
            painter.drawArc(arc, start, 110 * 16);
            painter.restore();
        } else if (entry.launched) {
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

    if (entries_.at(index).desktopMenu ||
        entries_.at(index).buildsDesktop ||
        entries_.at(index).launch == nullptr) {
        return;
    }

    if (entries_[index].launch()) {
        beginLoading(index);
    }
}

void DockBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const int index = entryAt(event->position().toPoint());

    if (index < 0) {
        return;
    }

    if (entries_.at(index).buildsDesktop) {
        startBuild(index);
        return;
    }

    if (!entries_.at(index).desktopMenu) {
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

void showBuildAbort(const QString &details)
{
    auto *dialog = new QDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Compilação abortada"));
    dialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
    dialog->resize(720, 420);

    auto *layout = new QVBoxLayout(dialog);
    auto *title = new QLabel(QStringLiteral(
        "O source do desktop não é válido. A compilação foi abortada e o Eden aberto não foi trocado."));
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *text = new QPlainTextEdit(details);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(text);

    auto *closeButton = new QPushButton(QStringLiteral("Fechar"));
    QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    layout->addWidget(closeButton);
    dialog->show();
    dialog->raise();
}

bool locateDesktopSources(QString &projectDir, QString &buildDir, QString &error)
{
    const QDir binaryDir(QCoreApplication::applicationDirPath());
    const QString cache = binaryDir.filePath(QStringLiteral("CMakeCache.txt"));
    const QString listed = QDir(binaryDir.absoluteFilePath(QStringLiteral(".."))).filePath(QStringLiteral("CMakeLists.txt"));

    if (QFileInfo::exists(cache)) {
        buildDir = binaryDir.absolutePath();
        projectDir = QDir(binaryDir.absoluteFilePath(QStringLiteral(".."))).absolutePath();
    } else if (QFileInfo::exists(listed)) {
        projectDir = QDir(binaryDir.absoluteFilePath(QStringLiteral(".."))).absolutePath();
        buildDir = binaryDir.absolutePath();
    } else {
        error = QStringLiteral("Não encontrei o CMakeLists.txt nem a pasta de build do Eden.");
        return false;
    }

    const QString source = QDir(projectDir).filePath(QStringLiteral("desktop/src/main.cpp"));

    if (!QFileInfo::exists(QDir(projectDir).filePath(QStringLiteral("CMakeLists.txt"))) ||
        !QFileInfo::exists(source)) {
        error = QStringLiteral("O source do desktop não está em ") + projectDir;
        return false;
    }

    if (!QFileInfo::exists(QDir(buildDir).filePath(QStringLiteral("CMakeCache.txt")))) {
        error = QStringLiteral("A pasta de build não está pronta. Rode cmake -S . -B build antes.");
        return false;
    }

    return true;
}

void DockBar::startBuild(int index)
{
    if (buildProcess_ != nullptr) {
        return;
    }

    QString projectDir;
    QString buildDir;
    QString error;

    if (!locateDesktopSources(projectDir, buildDir, error)) {
        showBuildAbort(error);
        return;
    }

    const QString cmake = QStandardPaths::findExecutable(QStringLiteral("cmake"));

    if (cmake.isEmpty()) {
        showBuildAbort(QStringLiteral("O cmake não está instalado."));
        return;
    }

    buildIndex_ = index;
    entries_[index].label = QStringLiteral("Compilando");
    beginLoading(index);

    buildProcess_ = new QProcess(this);
    buildProcess_->setWorkingDirectory(projectDir);
    buildProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(buildProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        finishBuild(exitCode, status == QProcess::NormalExit);
    });
    connect(buildProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (buildProcess_ == nullptr || buildProcess_->state() != QProcess::NotRunning) {
            return;
        }

        finishBuild(1, false);
    });
    buildProcess_->start(cmake, {
        QStringLiteral("--build"),
        buildDir,
        QStringLiteral("--target"),
        QStringLiteral("eden-desktop"),
        QStringLiteral("windowmanager-test"),
        QStringLiteral("--parallel")
    });
}

void DockBar::finishBuild(int exitCode, bool normalExit)
{
    if (buildIndex_ < 0) {
        return;
    }

    QString output;

    if (buildProcess_ != nullptr) {
        output = QString::fromUtf8(buildProcess_->readAll());
        buildProcess_->deleteLater();
        buildProcess_ = nullptr;
    }

    const bool compiled = normalExit && exitCode == 0;

    if (buildIndex_ >= 0 && buildIndex_ < entries_.size()) {
        entries_[buildIndex_].label = QStringLiteral("Compilar");
        entries_[buildIndex_].launched = compiled;
        buildIndex_ = -1;
        relayout();
    }

    if (!compiled) {
        const QStringList lines = output.split(QChar('\n'));
        const int begin = qMax(0, lines.size() - 40);
        showBuildAbort(lines.mid(begin).join(QChar('\n')).trimmed());
        return;
    }

    QString projectDir;
    QString buildDir;
    QString error;

    if (!locateDesktopSources(projectDir, buildDir, error)) {
        showBuildAbort(error);
        return;
    }

    const QString windowManager = QDir(buildDir).filePath(QStringLiteral("windowmanager-test"));
    const QString desktop = QCoreApplication::applicationFilePath();

    if (!launchAfterDelay(windowManager, "0.3") || !launchAfterDelay(desktop, "0.8")) {
        showBuildAbort(QStringLiteral(
            "A compilação foi válida, mas o Eden não conseguiu reabrir o desktop novo. A sessão atual foi mantida."));
        return;
    }

    stopDesktopTools();
    DesktopWindow::allowSessionExit();
    QCoreApplication::quit();
}
