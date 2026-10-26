#include "desktop/dockbar.hpp"
#include "desktop/desktoplaunch.hpp"
#include "desktop/desktopwindow.hpp"
#include "desktop/docksettings.hpp"
#include "desktop/glassstyle.hpp"
#include "desktop/notifyhub.hpp"
#include "desktop/sysmetrics.hpp"
#include "desktop/settingswindow.hpp"
#include "desktop/systemmanager.hpp"
#include "desktop/wallpapersettings.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QBitmap>
#include <QByteArray>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include <QCloseEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QImage>
#include <QLinearGradient>
#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QScreen>
#include <QShowEvent>
#include <QThread>
#include <QVariantAnimation>

#include <algorithm>
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
    return EdenWallpaper::currentPath();
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
    SettingsWindow::open();
    return true;
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

bool launchDetached(const QString &program)
{
    const QByteArray target = QFile::encodeName(program);
    char *argv[] = {
        const_cast<char *>(target.constData()),
        nullptr
    };

    posix_spawnattr_t attributes;

    if (posix_spawnattr_init(&attributes) != 0) {
        return false;
    }

    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
    pid_t pid = 0;
    const int result = posix_spawn(
        &pid,
        target.constData(),
        nullptr,
        &attributes,
        argv,
        environ);
    posix_spawnattr_destroy(&attributes);
    return result == 0 && pid > 0;
}

bool processAlive(qint64 pid)
{
    return pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0;
}

bool waitForProcessesGone(const QVector<qint64> &pids, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        bool anyAlive = false;

        for (qint64 pid : pids) {
            if (processAlive(pid)) {
                anyAlive = true;
                break;
            }
        }

        if (!anyAlive) {
            return true;
        }

        QThread::msleep(40);
    }

    for (qint64 pid : pids) {
        if (processAlive(pid)) {
            kill(static_cast<pid_t>(pid), SIGKILL);
        }
    }

    QThread::msleep(80);

    for (qint64 pid : pids) {
        if (processAlive(pid)) {
            return false;
        }
    }

    return true;
}

bool relaunchDesktopInPlace(const QString &desktopPath)
{
    const QByteArray path = QFile::encodeName(desktopPath);
    char *argv[] = {
        const_cast<char *>(path.constData()),
        nullptr
    };

    // Keep the same PID so the login session (exec eden-desktop) does not end.
    execv(path.constData(), argv);
    return false;
}

QVector<qint64> stopDesktopTools()
{
    QVector<qint64> stopped;
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

        if (name == QStringLiteral("eden-desktop") ||
            name.startsWith(QStringLiteral("windowmanager"))) {
            if (kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
                stopped.push_back(pid);
            }
        }
    }

    return stopped;
}

bool relaunchWindowManager(const QString &windowManager)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (launchDetached(windowManager)) {
            // Give the new WM time to claim SubstructureRedirect.
            QThread::msleep(180);
            return true;
        }

        QThread::msleep(120);
    }

    return false;
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

class DockPopup final : public QWidget
{
public:
    struct Row
    {
        QString text;
        bool selectable = true;
        bool marked = false;
    };

    explicit DockPopup(QVector<Row> rows)
        : QWidget(nullptr)
        , rows_(std::move(rows))
    {
        setWindowFlags(
            Qt::Popup |
            Qt::FramelessWindowHint |
            Qt::NoDropShadowWindowHint |
            Qt::WindowStaysOnTopHint);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        QFont itemFont = font();
        itemFont.setPixelSize(13);
        setFont(itemFont);
        setFixedSize(layoutSize());

        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    int execNear(const QPoint &globalTopCenter, const QRect &screen)
    {
        const QSize hint = size();
        const int minimumX = screen.left() + 8;
        const int maximumX = std::max(minimumX, screen.right() - hint.width() - 8);
        const int x = std::clamp(globalTopCenter.x() - hint.width() / 2, minimumX, maximumX);
        const int y = std::max(screen.top() + 8, globalTopCenter.y() - hint.height() - 10);
        move(x, y);
        show();
        raise();

        QEventLoop loop;
        loop_ = &loop;
        loop.exec();
        loop_ = nullptr;
        return chosen_;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#D2D2D7")), 1));
        painter.setBrush(QColor(QStringLiteral("#F5F5F7")));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);

        const bool reserveMark = std::any_of(rows_.cbegin(), rows_.cend(), [](const Row &row) {
            return row.marked;
        });

        for (int index = 0; index < rows_.size(); ++index) {
            const Row &row = rows_.at(index);
            const QRect box = rowRect(index);

            if (!row.selectable) {
                painter.setPen(QColor(QStringLiteral("#6E6E73")));
                QFont caption = font();
                caption.setWeight(QFont::DemiBold);
                painter.setFont(caption);
                painter.drawText(box.adjusted(12, 0, -10, -1), Qt::AlignVCenter | Qt::AlignLeft, row.text);
                painter.setFont(font());
                painter.setPen(QPen(QColor(QStringLiteral("#D2D2D7")), 1));
                painter.drawLine(box.left() + 10, box.bottom(), box.right() - 10, box.bottom());
                continue;
            }

            if (index == hover_) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#0A84FF")));
                painter.drawRoundedRect(box.adjusted(2, 2, -2, -2), 8, 8);
                painter.setPen(Qt::white);
            } else {
                painter.setPen(QColor(QStringLiteral("#1C1C1E")));
            }

            const QString prefix = row.marked
                ? QStringLiteral("✓   ")
                : (reserveMark ? QStringLiteral("     ") : QString());
            painter.drawText(box.adjusted(12, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, prefix + row.text);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int next = rowAt(event->position().toPoint());

        if (next == hover_) {
            return;
        }

        hover_ = next;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }

        const int index = rowAt(event->position().toPoint());

        if (index < 0) {
            return;
        }

        chosen_ = index;
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

    void closeEvent(QCloseEvent *event) override
    {
        QWidget::closeEvent(event);

        if (loop_ != nullptr) {
            loop_->quit();
        }
    }

private:
    [[nodiscard]] QSize layoutSize() const
    {
        const QFontMetrics metrics(font());
        int width = 168;

        for (const Row &row : rows_) {
            width = std::max(width, metrics.horizontalAdvance(row.text) + (row.marked ? 64 : 48));
        }

        int height = 8;

        for (const Row &row : rows_) {
            height += row.selectable ? 34 : 32;
        }

        return {width, height};
    }

    [[nodiscard]] QRect rowRect(int index) const
    {
        int y = 4;

        for (int row = 0; row < index; ++row) {
            y += rows_.at(row).selectable ? 34 : 32;
        }

        const int height = rows_.at(index).selectable ? 34 : 32;
        return QRect(4, y, width() - 8, height);
    }

    [[nodiscard]] int rowAt(const QPoint &position) const
    {
        for (int index = 0; index < rows_.size(); ++index) {
            if (!rows_.at(index).selectable || !rowRect(index).contains(position)) {
                continue;
            }

            return index;
        }

        return -1;
    }

    QVector<Row> rows_;
    QEventLoop *loop_ = nullptr;
    int hover_ = -1;
    int chosen_ = -1;
};

int runDockPopup(const QVector<DockPopup::Row> &rows, const QPoint &globalTopCenter, const QRect &screen)
{
    DockPopup popup(rows);
    return popup.execNear(globalTopCenter, screen);
}

QString dockConfigDir()
{
    const QString path = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(path);
    return path;
}

QString dockPinsPath()
{
    return QDir(dockConfigDir()).filePath(QStringLiteral("dock.pins"));
}

QString dockAppsDir()
{
    const QString path = QDir(dockConfigDir()).filePath(QStringLiteral("dock-apps"));
    QDir().mkpath(path);
    return path;
}

QString dockIconsDir()
{
    const QString path = QDir(dockConfigDir()).filePath(QStringLiteral("dock-icons"));
    QDir().mkpath(path);
    return path;
}

QString sanitizeDockKey(QString label)
{
    label = label.trimmed().toLower();
    label.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    label.remove(QRegularExpression(QStringLiteral("^-+|-+$")));

    if (label.isEmpty()) {
        label = QStringLiteral("app");
    }

    return label;
}

QString encodeList(const QStringList &values)
{
    return values.join(QLatin1Char('\x1f'));
}

QStringList decodeList(const QString &value)
{
    if (value.isEmpty()) {
        return {};
    }

    return value.split(QLatin1Char('\x1f'), Qt::KeepEmptyParts);
}

QStringList defaultPinnedLabels()
{
    return {
        QStringLiteral("Arquivos"),
        QStringLiteral("Terminal"),
        QStringLiteral("Editor"),
        QStringLiteral("Git"),
        QStringLiteral("Docker"),
        QStringLiteral("Banco"),
        QStringLiteral("API"),
        QStringLiteral("Pacotes"),
        QStringLiteral("Depurador")
    };
}

QStringList loadPinnedLabels()
{
    QFile file(dockPinsPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return defaultPinnedLabels();
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (!line.startsWith(QLatin1String("pinned="))) {
            continue;
        }

        const QString value = line.mid(7).trimmed();

        if (value.isEmpty()) {
            return {};
        }

        return value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    }

    return defaultPinnedLabels();
}

bool savePinnedLabels(const QStringList &labels)
{
    QSaveFile file(dockPinsPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "pinned=" << labels.join(QLatin1Char(',')) << '\n';
    return file.commit();
}

struct CustomDockApp
{
    QString label;
    QString iconName;
    QStringList launchArgs;
    QStringList matchClasses;
    QStringList matchTitles;
    bool terminal = false;
};

bool saveCustomDockApp(const CustomDockApp &app)
{
    if (app.label.trimmed().isEmpty()) {
        return false;
    }

    const QString key = sanitizeDockKey(app.label);
    QSaveFile file(QDir(dockAppsDir()).filePath(key + QStringLiteral(".app")));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "label=" << app.label << '\n';
    stream << "icon=" << app.iconName << '\n';
    stream << "terminal=" << (app.terminal ? 1 : 0) << '\n';
    stream << "exec=" << encodeList(app.launchArgs) << '\n';
    stream << "classes=" << encodeList(app.matchClasses) << '\n';
    stream << "titles=" << encodeList(app.matchTitles) << '\n';
    return file.commit();
}

void removeCustomDockApp(const QString &label)
{
    const QString key = sanitizeDockKey(label);
    QFile::remove(QDir(dockAppsDir()).filePath(key + QStringLiteral(".app")));
    QFile::remove(QDir(dockIconsDir()).filePath(key + QStringLiteral(".png")));
}

CustomDockApp loadCustomDockApp(const QString &label)
{
    CustomDockApp app;
    const QString key = sanitizeDockKey(label);
    QFile file(QDir(dockAppsDir()).filePath(key + QStringLiteral(".app")));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return app;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int split = line.indexOf(QLatin1Char('='));

        if (split <= 0) {
            continue;
        }

        const QString field = line.left(split);
        const QString value = line.mid(split + 1);

        if (field == QLatin1String("label")) {
            app.label = value;
        } else if (field == QLatin1String("icon")) {
            app.iconName = value;
        } else if (field == QLatin1String("terminal")) {
            app.terminal = value == QLatin1String("1") || value == QLatin1String("true");
        } else if (field == QLatin1String("exec")) {
            app.launchArgs = decodeList(value);
        } else if (field == QLatin1String("classes")) {
            app.matchClasses = decodeList(value);
        } else if (field == QLatin1String("titles")) {
            app.matchTitles = decodeList(value);
        }
    }

    if (app.label.isEmpty()) {
        app.label = label;
    }

    return app;
}

QPixmap resolveDockIcon(const QString &label, const QString &iconName, int size)
{
    if (!iconName.isEmpty()) {
        if (iconName.contains(QLatin1Char('/'))) {
            QPixmap pixmap(iconName);

            if (!pixmap.isNull()) {
                return pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        } else {
            const QIcon theme = QIcon::fromTheme(iconName);

            if (!theme.isNull()) {
                return theme.pixmap(size, size);
            }
        }
    }

    const QString cached = QDir(dockIconsDir()).filePath(
        sanitizeDockKey(label) + QStringLiteral(".png"));
    QPixmap pixmap(cached);

    if (!pixmap.isNull()) {
        return pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return {};
}

QString cacheDockIcon(const QString &label, const QPixmap &icon)
{
    if (icon.isNull()) {
        return {};
    }

    const QString path = QDir(dockIconsDir()).filePath(
        sanitizeDockKey(label) + QStringLiteral(".png"));
    icon.save(path, "PNG");
    return path;
}

class WindowChooser;

WindowChooser *activeWindowChooser = nullptr;
QString activeWindowChooserLabel;

class WindowChooser final : public QWidget
{
public:
    WindowChooser(
        QVector<EdenDesktop::ClientWindow> windows,
        const QPixmap &fallbackIcon,
        const QRect &bounds)
        : QWidget(nullptr)
        , windows_(std::move(windows))
        , fallbackIcon_(fallbackIcon)
        , bounds_(bounds)
    {
        setWindowFlags(
            Qt::Tool |
            Qt::FramelessWindowHint |
            Qt::WindowStaysOnTopHint |
            Qt::BypassWindowManagerHint |
            Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setGeometry(bounds_);

        previews_.reserve(windows_.size());

        for (const EdenDesktop::ClientWindow &client : windows_) {
            previews_.push_back(EdenDesktop::captureWindowPixmap(client.window));
        }

        layoutTiles();

        animation_ = new QVariantAnimation(this);
        animation_->setDuration(220);
        animation_->setStartValue(0.0);
        animation_->setEndValue(1.0);
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress_ = value.toReal();
            update();
        });
        connect(animation_, &QVariantAnimation::finished, this, [this]() {
            if (closing_) {
                hide();
                releaseActive();
                deleteLater();
            }
        });
    }

    ~WindowChooser() override
    {
        releaseActive();
    }

    void openAnimated()
    {
        closing_ = false;
        progress_ = 0.0;
        show();
        raise();
        activateWindow();
        setFocus();
        animation_->stop();
        animation_->setDirection(QAbstractAnimation::Forward);
        animation_->start();
    }

    void dismiss()
    {
        beginClose();
    }

    void reopen()
    {
        if (!closing_) {
            return;
        }

        closing_ = false;
        animation_->stop();
        animation_->setDirection(QAbstractAnimation::Forward);
        animation_->start();
        raise();
        activateWindow();
        setFocus();
    }

    [[nodiscard]] bool closing() const
    {
        return closing_;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const qreal fade = std::clamp(progress_, 0.0, 1.0);
        const qreal zoom = 0.86 + (0.14 * fade);

        painter.fillRect(rect(), QColor(12, 14, 20, static_cast<int>(200 * fade)));

        painter.setOpacity(fade);
        painter.setPen(QColor(245, 245, 247));
        QFont titleFont = font();
        titleFont.setPixelSize(15);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(
            QRect(0, 28, width(), 28),
            Qt::AlignCenter,
            QStringLiteral("Escolha a janela"));

        for (int index = 0; index < tiles_.size(); ++index) {
            const QRect tile = tiles_.at(index);
            const bool hovered = index == hover_ && !closing_;
            const QPoint center = tile.adjusted(-10, -10, 10, 10).center();

            painter.save();
            painter.translate(center);
            painter.scale(zoom, zoom);
            painter.translate(-center);

            QRect frame = tile.adjusted(-10, -10, 10, 10);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, hovered ? 36 : 18));
            painter.drawRoundedRect(frame, 18, 18);

            if (hovered) {
                painter.setPen(QPen(QColor(245, 245, 247, 220), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(frame.adjusted(1, 1, -1, -1), 17, 17);
            }

            const QPixmap &preview = previews_.at(index);

            if (!preview.isNull()) {
                painter.drawPixmap(tile, preview.scaled(
                    tile.size(),
                    Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation));
            } else if (!fallbackIcon_.isNull()) {
                const QSize iconSize(
                    std::min(96, tile.width() / 2),
                    std::min(96, tile.height() / 2));
                const QRect iconRect(
                    tile.center().x() - iconSize.width() / 2,
                    tile.center().y() - iconSize.height() / 2,
                    iconSize.width(),
                    iconSize.height());
                painter.fillRect(tile, QColor(40, 44, 54));
                painter.drawPixmap(iconRect, fallbackIcon_.scaled(
                    iconSize,
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
            } else {
                painter.fillRect(tile, QColor(40, 44, 54));
            }

            const QString caption = windows_.at(index).title.trimmed().isEmpty()
                ? QStringLiteral("Janela %1").arg(index + 1)
                : windows_.at(index).title;
            painter.setPen(QColor(245, 245, 247));
            QFont captionFont = font();
            captionFont.setPixelSize(12);
            painter.setFont(captionFont);
            painter.drawText(
                QRect(frame.left(), frame.bottom() + 8, frame.width(), 22),
                Qt::AlignHCenter | Qt::AlignTop,
                QFontMetrics(captionFont).elidedText(
                    caption,
                    Qt::ElideRight,
                    frame.width()));
            painter.restore();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (closing_) {
            return;
        }

        const int next = tileAt(event->position().toPoint());

        if (next == hover_) {
            return;
        }

        hover_ = next;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        if (hover_ < 0) {
            return;
        }

        hover_ = -1;
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (closing_ || event->button() != Qt::LeftButton) {
            return;
        }

        const int index = tileAt(event->position().toPoint());

        if (index >= 0 && index < windows_.size()) {
            EdenDesktop::activateWindow(windows_.at(index).window);
            beginClose();
            return;
        }

        beginClose();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            beginClose();
            return;
        }

        QWidget::keyPressEvent(event);
    }

private:
    void releaseActive()
    {
        if (activeWindowChooser != this) {
            return;
        }

        activeWindowChooser = nullptr;
        activeWindowChooserLabel.clear();
    }

    void beginClose()
    {
        if (closing_) {
            return;
        }

        closing_ = true;
        animation_->stop();
        animation_->setDirection(QAbstractAnimation::Backward);
        animation_->start();
    }

    void layoutTiles()
    {
        tiles_.clear();

        const int count = windows_.size();

        if (count <= 0) {
            return;
        }

        const int gap = 28;
        const int sideMargin = 64;
        const int top = 88;
        const int bottom = 56;
        const int availW = std::max(160, width() - sideMargin * 2 - gap * (count - 1));
        const int availH = std::max(120, height() - top - bottom);
        const int cellW = availW / count;
        int x = sideMargin;

        for (int index = 0; index < count; ++index) {
            const QSize source = !previews_.at(index).isNull()
                ? previews_.at(index).size()
                : QSize(16, 10);
            const qreal aspect = source.height() > 0
                ? static_cast<qreal>(source.width()) / source.height()
                : 1.6;
            int tileW = cellW;
            int tileH = static_cast<int>(tileW / aspect);

            if (tileH > availH) {
                tileH = availH;
                tileW = static_cast<int>(tileH * aspect);
            }

            tileW = std::min(tileW, cellW);
            tileH = std::min(tileH, availH);

            const QRect tile(
                x + (cellW - tileW) / 2,
                top + (availH - tileH) / 2,
                tileW,
                tileH);
            tiles_.push_back(tile);
            x += cellW + gap;
        }
    }

    [[nodiscard]] int tileAt(const QPoint &position) const
    {
        for (int index = 0; index < tiles_.size(); ++index) {
            if (tiles_.at(index).adjusted(-14, -14, 14, 36).contains(position)) {
                return index;
            }
        }

        return -1;
    }

    QVector<EdenDesktop::ClientWindow> windows_;
    QVector<QPixmap> previews_;
    QVector<QRect> tiles_;
    QPixmap fallbackIcon_;
    QRect bounds_;
    QVariantAnimation *animation_ = nullptr;
    qreal progress_ = 0.0;
    int hover_ = -1;
    bool closing_ = false;
};

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

    settings_ = EdenDock::load();
    syncLabelFont();
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

    syncTimer_ = new QTimer(this);
    connect(syncTimer_, &QTimer::timeout, this, [this]() {
        syncOpenWindows();
    });
    syncTimer_->start(700);
    QTimer::singleShot(0, this, [this]() {
        syncOpenWindows();
    });

    auto *meters = new QTimer(this);
    connect(meters, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            update();
        }
    });
    meters->start(1000);

    EdenGlass::watch(this, [this]() {
        refreshGlass();
        compose();
        update();
    });
    EdenNotify::watch(this, [this]() {
        update();
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

EdenDock::Settings DockBar::settings() const
{
    return settings_;
}

void DockBar::syncLabelFont()
{
    QFont labelFont = font();
    labelFont.setPointSize(settings_.fontSize);
    labelFont.setWeight(QFont::DemiBold);
    setFont(labelFont);
}

void DockBar::applySettings(const EdenDock::Settings &settings)
{
    settings_ = settings;
    settings_.clamp();
    syncLabelFont();
    glass_ = QPixmap();
    composed_ = QPixmap();
    relayout();
    publishDockStrut();
}

int DockBar::labelHeight() const
{
    return std::max(24, settings_.fontSize + 16);
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

bool DockBar::anyLoading() const
{
    for (const Entry &entry : entries_) {
        if (entry.loading) {
            return true;
        }
    }

    return false;
}

void DockBar::beginLoading(int index)
{
    if (index < 0 || index >= entries_.size() || entries_.at(index).separator) {
        return;
    }

    entries_[index].loading = true;
    entries_[index].launched = false;
    entries_[index].loadingStarted = QDateTime::currentMSecsSinceEpoch();

    if (!loadingTimer_->isActive()) {
        loadingTimer_->start(16);
    }

    updateMask();
    update();
}

void DockBar::rememberPid(int index, qint64 pid)
{
    if (index < 0 || index >= entries_.size() || pid <= 0) {
        return;
    }

    if (!entries_[index].pids.contains(pid)) {
        entries_[index].pids.push_back(pid);
    }
}

void DockBar::notifyOpening(
    const QString &label,
    const QIcon &icon,
    const QStringList &matchClasses,
    qint64 pid,
    const QStringList &matchTitles,
    const QStringList &launchArgs,
    bool terminal,
    const QString &iconName)
{
    if (label.trimmed().isEmpty()) {
        return;
    }

    const int existing = findEntry(label);

    if (existing >= 0) {
        for (const QString &hint : matchClasses) {
            if (!hint.isEmpty() &&
                !entries_[existing].matchClasses.contains(hint, Qt::CaseInsensitive)) {
                entries_[existing].matchClasses.push_back(hint);
            }
        }

        for (const QString &hint : matchTitles) {
            if (!hint.isEmpty() &&
                !entries_[existing].matchTitles.contains(hint, Qt::CaseInsensitive)) {
                entries_[existing].matchTitles.push_back(hint);
            }
        }

        if (entries_[existing].launchArgs.isEmpty() && !launchArgs.isEmpty()) {
            entries_[existing].launchArgs = launchArgs;
            entries_[existing].terminal = terminal;
        }

        if (entries_[existing].iconName.isEmpty() && !iconName.isEmpty()) {
            entries_[existing].iconName = iconName;
        }

        rememberPid(existing, pid);
        beginLoading(existing);
        return;
    }

    Entry entry;
    entry.label = label.trimmed();
    entry.ephemeral = true;
    entry.matchClasses = matchClasses;
    entry.matchTitles = matchTitles;
    entry.launchArgs = launchArgs;
    entry.terminal = terminal;
    entry.iconName = iconName;
    entry.icon = icon.pixmap(settings_.iconHoverSize, settings_.iconHoverSize);
    restoreBuiltinHooks(entry);

    if (entry.icon.isNull()) {
        entry.icon = resolveDockIcon(
            entry.label,
            entry.iconName,
            settings_.iconHoverSize);
    }

    if (entry.icon.isNull()) {
        QPixmap fallback(settings_.iconHoverSize, settings_.iconHoverSize);
        fallback.fill(Qt::transparent);
        QPainter painter(&fallback);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0, 229, 255));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(fallback.rect().adjusted(6, 6, -6, -6), 14, 14);
        entry.icon = fallback;
    }

    const int insertAt = separatorIndex();
    entries_.insert(insertAt, entry);
    rememberPid(insertAt, pid);
    relayout();
    beginLoading(insertAt);
}

void DockBar::tickLoading()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool finished = false;

    for (Entry &entry : entries_) {
        if (!entry.loading) {
            continue;
        }

        if (now - entry.loadingStarted >= kBounceMs) {
            entry.loading = false;
            entry.launched = true;
            finished = true;
        }
    }

    if (!anyLoading()) {
        loadingTimer_->stop();
        updateMask();
    }

    if (finished) {
        updateMask();
    }

    update();
}

void DockBar::buildEntries()
{
    struct Spec
    {
        const char *label;
        const char *icon;
        bool separator;
        bool (*launch)();
        QStringList matchClasses;
        QStringList matchTitles;
        bool closeable;
        bool trailing;
    };

    const Spec specs[] = {
        {
            "Arquivos",
            "eden-launcher.svg",
            false,
            launchFiles,
            {
                QStringLiteral("edens"),
                QStringLiteral("Edens"),
                QStringLiteral("eden-file-manager")
            },
            {QStringLiteral("Eden File Manager")},
            true,
            false
        },
        {
            "Terminal",
            "eden-terminal.svg",
            false,
            launchTerminal,
            {
                QStringLiteral("gnome-terminal"),
                QStringLiteral("Gnome-terminal"),
                QStringLiteral("xfce4-terminal"),
                QStringLiteral("mate-terminal"),
                QStringLiteral("xterm"),
                QStringLiteral("XTerm"),
                QStringLiteral("kitty"),
                QStringLiteral("Alacritty")
            },
            {},
            true,
            false
        },
        {
            "Editor",
            "eden-ide.svg",
            false,
            launchIde,
            {
                QStringLiteral("code"),
                QStringLiteral("Code"),
                QStringLiteral("codium"),
                QStringLiteral("VSCodium"),
                QStringLiteral("gnome-text-editor"),
                QStringLiteral("gedit"),
                QStringLiteral("xed"),
                QStringLiteral("mousepad"),
                QStringLiteral("kate"),
                QStringLiteral("Kate")
            },
            {},
            true,
            false
        },
        {
            "Git",
            "eden-git.svg",
            false,
            launchGit,
            {QStringLiteral("git-gui"), QStringLiteral("GitGui"), QStringLiteral("gitk")},
            {},
            true,
            false
        },
        {
            "Docker",
            "eden-docker.svg",
            false,
            launchDocker,
            {QStringLiteral("docker"), QStringLiteral("Docker Desktop")},
            {},
            true,
            false
        },
        {
            "Banco",
            "eden-database.svg",
            false,
            launchDatabase,
            {
                QStringLiteral("dbeaver"),
                QStringLiteral("DBeaver"),
                QStringLiteral("mysql-workbench"),
                QStringLiteral("MySQLWorkbench")
            },
            {},
            true,
            false
        },
        {
            "API",
            "eden-api.svg",
            false,
            launchApi,
            {
                QStringLiteral("insomnia"),
                QStringLiteral("Insomnia"),
                QStringLiteral("postman"),
                QStringLiteral("Postman")
            },
            {},
            true,
            false
        },
        {
            "Pacotes",
            "eden-package.svg",
            false,
            launchPackage,
            {
                QStringLiteral("synaptic"),
                QStringLiteral("Synaptic"),
                QStringLiteral("gnome-software"),
                QStringLiteral("GnomeSoftware")
            },
            {},
            true,
            false
        },
        {
            "Depurador",
            "eden-debugger.svg",
            false,
            launchDebugger,
            {QStringLiteral("gdbgui"), QStringLiteral("Gdbgui")},
            {},
            true,
            false
        },
        {"", "", true, nullptr, {}, {}, false, true},
        {"Áreas", "", false, nullptr, {}, {}, false, true},
        {
            "Ajustes",
            "eden-settings.svg",
            false,
            launchSettings,
            {},
            {QStringLiteral("Ajustes do Sistema")},
            true,
            true
        },
        {"Compilar", "", false, nullptr, {}, {}, false, true}
    };

    const auto makeBuiltin = [&](const Spec &spec) {
        Entry entry;
        entry.label = QString::fromUtf8(spec.label);
        entry.separator = spec.separator;
        entry.launch = spec.launch;
        entry.matchClasses = spec.matchClasses;
        entry.matchTitles = spec.matchTitles;
        entry.closeable = spec.closeable;
        entry.builtin = !spec.separator && !spec.trailing;
        entry.ephemeral = false;

        if (entry.label == QStringLiteral("Áreas")) {
            entry.desktopMenu = true;
            entry.closeable = false;
            entry.icon = spacesIcon();
        } else if (entry.label == QStringLiteral("Compilar")) {
            entry.buildsDesktop = true;
            entry.closeable = false;
            entry.icon = buildIcon();
        } else if (!entry.separator) {
            entry.icon = loadIcon(QString::fromUtf8(spec.icon));
            entry.iconName = QString::fromUtf8(spec.icon);
        }

        return entry;
    };

    QHash<QString, Spec> builtins;

    for (const Spec &spec : specs) {
        if (spec.trailing || spec.separator || spec.label[0] == '\0') {
            continue;
        }

        builtins.insert(QString::fromUtf8(spec.label), spec);
    }

    entries_.clear();
    QSet<QString> seen;
    const QStringList pinned = loadPinnedLabels();

    for (const QString &label : pinned) {
        const QString trimmed = label.trimmed();

        if (trimmed.isEmpty() || seen.contains(trimmed.toLower())) {
            continue;
        }

        if (builtins.contains(trimmed)) {
            entries_.push_back(makeBuiltin(builtins.value(trimmed)));
            seen.insert(trimmed.toLower());
            continue;
        }

        const CustomDockApp custom = loadCustomDockApp(trimmed);

        if (custom.launchArgs.isEmpty() && custom.label.isEmpty()) {
            continue;
        }

        Entry entry;
        entry.label = custom.label.isEmpty() ? trimmed : custom.label;
        entry.iconName = custom.iconName;
        entry.launchArgs = custom.launchArgs;
        entry.matchClasses = custom.matchClasses;
        entry.matchTitles = custom.matchTitles;
        entry.terminal = custom.terminal;
        entry.ephemeral = false;
        entry.closeable = true;
        entry.icon = resolveDockIcon(
            entry.label,
            entry.iconName,
            settings_.iconHoverSize);

        if (entry.icon.isNull()) {
            QPixmap fallback(settings_.iconHoverSize, settings_.iconHoverSize);
            fallback.fill(Qt::transparent);
            QPainter painter(&fallback);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor(0, 229, 255));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(fallback.rect().adjusted(6, 6, -6, -6), 14, 14);
            entry.icon = fallback;
        }

        entries_.push_back(entry);
        seen.insert(entry.label.toLower());
    }

    for (const Spec &spec : specs) {
        if (!spec.trailing) {
            continue;
        }

        entries_.push_back(makeBuiltin(spec));
    }
}

void DockBar::place(const QRect &screen)
{
    screen_ = screen;
    relayout();
    publishDockStrut();
}

int DockBar::iconSize(int index) const
{
    if (index < 0 || index >= entries_.size() || entries_.at(index).separator) {
        return 0;
    }

    return index == hover_ ? settings_.iconHoverSize : settings_.iconSize;
}

double DockBar::bounceProgress(int index) const
{
    if (index < 0 || index >= entries_.size() || !entries_.at(index).loading) {
        return 1.0;
    }

    const qint64 elapsed =
        QDateTime::currentMSecsSinceEpoch() - entries_.at(index).loadingStarted;
    return std::clamp(elapsed / static_cast<double>(kBounceMs), 0.0, 1.0);
}

int DockBar::bounceOffset(int index) const
{
    const double progress = bounceProgress(index);

    if (progress >= 1.0) {
        return 0;
    }

    // Três pulos estilo Dock do macOS, com amplitude caindo.
    const double segment = progress * 3.0;
    const int bounceIndex = std::min(2, static_cast<int>(segment));
    const double local = segment - bounceIndex;
    const double amplitude = kBouncePeak * std::pow(0.55, bounceIndex);
    return static_cast<int>(-amplitude * std::sin(local * 3.14159265358979323846));
}

void DockBar::relayout()
{
    const int iconHover = settings_.iconHoverSize;
    const int gap = settings_.iconGap;
    const int labelH = labelHeight();
    int contentWidth = kPadX;

    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).separator) {
            contentWidth += 18;
            continue;
        }

        contentWidth += iconHover + gap;
    }

    contentWidth += kPadX - gap;
    contentWidth += kPerfWidth;

    const int barHeight = kPadY + iconHover + 8 + kPadY;
    const int totalHeight = labelH + kLabelGap + barHeight;
    const int x = screen_.x() + qMax(0, (screen_.width() - contentWidth) / 2);
    const int y = screen_.y() + screen_.height() - settings_.bottomMargin - totalHeight;

    const QRect nextGeometry(x, y, qMax(contentWidth, 1), totalHeight);

    if (geometry() != nextGeometry) {
        setGeometry(nextGeometry);
    }
    barRect_ = QRect(0, labelH + kLabelGap, width(), barHeight);

    iconRects_.resize(entries_.size());
    int cursor = kPadX;

    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).separator) {
            iconRects_[index] = QRect(cursor, barRect_.top(), 18, barRect_.height());
            cursor += 18;
            continue;
        }

        const int slot = iconHover;
        const int size = iconSize(index);
        const int iconX = cursor + (slot - size) / 2;
        const int iconY = barRect_.bottom() - kPadY - 8 - size;
        iconRects_[index] = QRect(iconX, iconY, size, size);
        cursor += slot + gap;
    }

    perfRect_ = QRect(
        barRect_.right() - kPerfWidth - 8,
        barRect_.top() + 8,
        kPerfWidth,
        qMax(1, barRect_.height() - 16));

    labelRect_ = QRect();

    if (hover_ >= 0 && hover_ < entries_.size() && !entries_.at(hover_).separator) {
        const QFontMetrics metrics(font());
        const int textWidth = metrics.horizontalAdvance(entries_.at(hover_).label) + 18;
        const QRect icon = iconRects_.at(hover_);
        const int labelX = qBound(4, icon.center().x() - textWidth / 2, width() - textWidth - 4);
        labelRect_ = QRect(labelX, 0, textWidth, labelH);
    }

    updateMask();

    if (glass_.size() != barRect_.size()) {
        refreshGlass();
    }

    compose();
    update();
}

QPainterPath DockBar::barPath() const
{
    QPainterPath path;
    path.addRoundedRect(barRect_.adjusted(0, 0, -1, -1), EdenGlass::kRadius, EdenGlass::kRadius);
    return path;
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

    // Only paint inside the rounded capsule — filling the full rect left a
    // dark rectangular “frame” around the dock when windows sat behind it.
    const QPainterPath capsule = barPath();
    painter.setClipPath(capsule);

    if (!backdrop_.isNull()) {
        painter.drawPixmap(0, 0, backdrop_);
    } else {
        painter.fillPath(capsule, EdenGlass::current().panelColor());
    }

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
    publishDockStrut();
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
        plate.fill(QColor(18, 24, 32));
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
            plate.fill(QColor(18, 24, 32));
        } else {
            makeOpaque(piece);
            const int divisor = EdenGlass::current().blurDivisor();
            const QImage small = piece.scaled(
                qMax(1, piece.width() / divisor),
                qMax(1, piece.height() / divisor),
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
            plate = small.scaled(piece.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            makeOpaque(plate);
        }
    }

    const EdenGlass::Appearance look = EdenGlass::current();
    QPainter frost(&plate);
    frost.fillRect(plate.rect(), look.frostColor());

    QLinearGradient sheen(0, 0, 0, plate.height());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 24 + look.liquid / 5));
    sheen.setColorAt(0.45, QColor(0, 229, 255, look.dark ? 16 : 8));
    sheen.setColorAt(1.0, QColor(look.dark ? 30 : 255, look.dark ? 30 : 255, look.dark ? 46 : 255, 30));
    frost.fillRect(plate.rect(), sheen);
    frost.end();
    makeOpaque(plate);

    glass_ = QPixmap::fromImage(plate);

    backdrop_ = QPixmap(size());
    backdrop_.fill(Qt::transparent);

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
    if (width() <= 0 || height() <= 0) {
        clearMask();
        return;
    }

    QBitmap bitmap(size());
    bitmap.fill(Qt::color0);

    QPainter painter(&bitmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::color1);
    painter.drawRoundedRect(barRect_.adjusted(0, 0, -1, -1), EdenGlass::kRadius, EdenGlass::kRadius);

    if (!labelRect_.isNull()) {
        painter.drawRoundedRect(labelRect_, 10, 10);
    }

    if (anyLoading()) {
        for (int index = 0; index < iconRects_.size(); ++index) {
            if (!entries_.at(index).loading) {
                continue;
            }

            painter.drawRect(
                iconRects_.at(index).adjusted(-2, -(kBouncePeak + 4), 2, 2));
        }
    }

    painter.end();
    setMask(bitmap);
}

void DockBar::publishDockStrut()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

    if (native == nullptr || native->connection() == nullptr || !isVisible()) {
        return;
    }

    xcb_connection_t *connection = native->connection();
    const xcb_window_t window = static_cast<xcb_window_t>(winId());
    const xcb_atom_t typeProperty = internAtom(connection, "_NET_WM_WINDOW_TYPE");
    const xcb_atom_t dockType = internAtom(connection, "_NET_WM_WINDOW_TYPE_DOCK");

    if (typeProperty != XCB_ATOM_NONE && dockType != XCB_ATOM_NONE) {
        xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            typeProperty,
            XCB_ATOM_ATOM,
            32,
            1,
            &dockType);
    }

    // Reserve from the top of the dock capsule to the bottom of the root.
    const QRect globalBar(
        mapToGlobal(barRect_.topLeft()),
        barRect_.size());
    const xcb_setup_t *setup = xcb_get_setup(connection);
    const xcb_screen_t *xscreen =
        setup == nullptr ? nullptr : xcb_setup_roots_iterator(setup).data;
    const int rootHeight =
        xscreen == nullptr ? screen_.height() : xscreen->height_in_pixels;
    const int bottomReserve = std::max(0, rootHeight - globalBar.top());

    const xcb_atom_t strut = internAtom(connection, "_NET_WM_STRUT");
    const uint32_t reserved[] = {0, 0, 0, static_cast<uint32_t>(bottomReserve)};

    if (strut != XCB_ATOM_NONE) {
        xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            strut,
            XCB_ATOM_CARDINAL,
            32,
            4,
            reserved);
    }

    const xcb_atom_t partial = internAtom(connection, "_NET_WM_STRUT_PARTIAL");
    const uint32_t left = static_cast<uint32_t>(std::max(0, globalBar.left()));
    const uint32_t right = static_cast<uint32_t>(
        std::max(0, globalBar.right()));
    const uint32_t partialValues[] = {
        0,
        0,
        0,
        static_cast<uint32_t>(bottomReserve),
        0,
        0,
        0,
        0,
        0,
        0,
        left,
        right};

    if (partial != XCB_ATOM_NONE) {
        xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            partial,
            XCB_ATOM_CARDINAL,
            32,
            12,
            partialValues);
    }

    // Remap so the window manager re-reads struts and updates the work area.
    xcb_unmap_window(connection, window);
    xcb_map_window(connection, window);
    xcb_flush(connection);
}

int DockBar::entryAt(const QPoint &position) const
{
    int nearest = -1;
    int nearestDistance = 0;

    for (int index = 0; index < iconRects_.size(); ++index) {
        if (entries_.at(index).separator) {
            continue;
        }

        const int extra =
            (settings_.iconHoverSize - iconRects_.at(index).width()) / 2 +
            settings_.iconGap / 2;
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

    painter.setBrush(Qt::NoBrush);
    const EdenGlass::Appearance look = EdenGlass::current();
    painter.setPen(QPen(look.borderColor(), 1));
    painter.drawPath(barPath());

    for (int index = 0; index < entries_.size(); ++index) {
        const Entry &entry = entries_.at(index);
        const QRect rect = iconRects_.at(index);

        if (entry.separator) {
            painter.setPen(QPen(look.borderColor(), 1));
            const int lineX = rect.center().x();
            painter.drawLine(lineX, barRect_.top() + 16, lineX, barRect_.bottom() - 16);
            continue;
        }

        painter.setPen(Qt::NoPen);

        const QRect drawn = entry.loading ? rect.translated(0, bounceOffset(index)) : rect;

        if (!entry.icon.isNull()) {
            painter.drawPixmap(drawn, entry.icon);
        } else {
            painter.setBrush(look.accentColor());
            painter.drawRoundedRect(drawn, 12, 12);
        }

        if (entry.openWindows > 0 && !entry.loading) {
            const int dots = std::min(entry.openWindows, kMaxDots);
            const int spacing = 7;
            const int span = (dots - 1) * spacing;
            const int baseX = rect.center().x() - span / 2;
            const int baseY = barRect_.bottom() - 8;
            painter.setBrush(look.accentColor());

            for (int dot = 0; dot < dots; ++dot) {
                painter.drawEllipse(QPoint(baseX + dot * spacing, baseY), 2, 2);
            }
        }

        const int badges = EdenNotify::countFor(entry.label, entry.iconName);

        if (badges > 0) {
            const QString text = badges > 9 ? QStringLiteral("9+") : QString::number(badges);
            QFont badgeFont = painter.font();
            badgeFont.setPointSize(8);
            badgeFont.setBold(true);
            painter.setFont(badgeFont);
            const int diameter = text.size() > 1 ? 18 : 16;
            const QRect badge(drawn.right() - diameter + 6, drawn.top() - 2, diameter, diameter);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 69, 58));
            painter.drawEllipse(badge);
            painter.setPen(Qt::white);
            painter.drawText(badge, Qt::AlignCenter, text);
        }
    }

    if (!perfRect_.isEmpty()) {
        const EdenMetrics::Snapshot metrics = EdenMetrics::current();
        painter.setPen(QPen(look.borderColor(), 1));
        painter.drawLine(perfRect_.left() - 4, perfRect_.top() + 6, perfRect_.left() - 4, perfRect_.bottom() - 16);

        const auto ring = [&painter](const QRect &rect, double percent, const QColor &color, const QString &caption) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 40), 3));
            painter.drawEllipse(rect);
            painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
            const int span = static_cast<int>(-std::clamp(percent, 0.0, 100.0) / 100.0 * 360.0 * 16.0);
            painter.drawArc(rect, 90 * 16, span);
            QFont font = painter.font();
            font.setPointSize(7);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(rect, Qt::AlignCenter, caption);
        };

        const QRect cpu(perfRect_.left() + 4, perfRect_.top(), 30, 30);
        const QRect ram(cpu.right() + 8, cpu.top(), 30, 30);
        ring(cpu, metrics.cpuOk ? metrics.cpuPercent : 0, look.accentColor(), QStringLiteral("CPU"));
        ring(ram, metrics.ramOk ? metrics.ramPercent : 0, QColor(QStringLiteral("#C4B5FD")), QStringLiteral("RAM"));

        QFont font = painter.font();
        font.setPointSize(8);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(look.textColor());
        const QString temp = metrics.tempOk
            ? QStringLiteral("%1° / %2°").arg(metrics.tempAvgC).arg(metrics.tempMaxC)
            : QStringLiteral("—");
        painter.drawText(
            QRect(perfRect_.left(), ram.bottom() + 2, perfRect_.width(), 16),
            Qt::AlignCenter,
            temp);
    }

    if (labelRect_.isNull()) {
        return;
    }

    painter.setFont(font());
    painter.setPen(Qt::NoPen);
    painter.setBrush(look.panelColor());
    painter.drawRoundedRect(labelRect_, 10, 10);
    painter.setPen(look.textColor());
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

int DockBar::openWindowCount(int index) const
{
    if (index < 0 || index >= entries_.size()) {
        return 0;
    }

    const Entry &entry = entries_.at(index);

    if (entry.separator || entry.desktopMenu || entry.buildsDesktop) {
        return 0;
    }

    if (entry.label == QStringLiteral("Ajustes")) {
        if (SettingsWindow::instance() != nullptr) {
            return 1;
        }
    }

    if (entry.label == QStringLiteral("Gerenciador do Sistema")) {
        if (SystemManager::instance() != nullptr) {
            return 1;
        }
    }

    return EdenDesktop::matchingWindows(
               entry.matchClasses,
               entry.pids,
               entry.matchTitles)
        .size();
}

void DockBar::syncOpenWindows()
{
    bool changed = false;
    QVector<int> removeEphemeral;
    static uint32_t lastDesktop = 0xFFFFFFFFu;
    const uint32_t desktop = currentDesktop();

    if (desktop != lastDesktop) {
        lastDesktop = desktop;
        changed = true;
    }

    for (int index = 0; index < entries_.size(); ++index) {
        Entry &entry = entries_[index];

        if (entry.separator || entry.desktopMenu || entry.buildsDesktop) {
            continue;
        }

        const int previous = entry.openWindows;
        const int count = openWindowCount(index);
        // Janelas em outro desktop não contam no ponto, mas o app continua
        // aberto. Zerar o PID aqui faz o Chrome (instância única) sumir ao voltar.
        const int anywhere = count > 0
            ? count
            : EdenDesktop::matchingWindows(
                  entry.matchClasses,
                  entry.pids,
                  entry.matchTitles,
                  false)
                  .size();

        if (previous != count) {
            entry.openWindows = count;
            changed = true;
        }

        if (entry.loading && anywhere == 0) {
            continue;
        }

        if (anywhere > 0) {
            entry.sawWindow = true;

            if (!entry.launched || entry.loading) {
                entry.launched = true;
                entry.loading = false;
                changed = true;
            }

            continue;
        }

        if (!entry.sawWindow) {
            continue;
        }

        entry.sawWindow = false;
        entry.launched = false;
        entry.pids.clear();
        entry.openWindows = 0;
        changed = true;

        if (entry.ephemeral) {
            removeEphemeral.push_back(index);
        }
    }

    for (int offset = removeEphemeral.size() - 1; offset >= 0; --offset) {
        const int index = removeEphemeral.at(offset);
        entries_.removeAt(index);

        if (hover_ == index) {
            hover_ = -1;
        } else if (hover_ > index) {
            --hover_;
        }

        changed = true;
    }

    if (!removeEphemeral.isEmpty()) {
        relayout();
        return;
    }

    if (changed) {
        if (!anyLoading()) {
            loadingTimer_->stop();
        }

        update();
    }
}

void DockBar::showWindowChooser(int index)
{
    if (index < 0 || index >= entries_.size()) {
        return;
    }

    const Entry &entry = entries_.at(index);

    if (activeWindowChooser != nullptr && activeWindowChooserLabel == entry.label) {
        if (activeWindowChooser->closing()) {
            activeWindowChooser->reopen();
        } else {
            activeWindowChooser->dismiss();
        }

        return;
    }

    const QVector<EdenDesktop::ClientWindow> windows = EdenDesktop::matchingWindows(
        entry.matchClasses,
        entry.pids,
        entry.matchTitles);

    if (windows.size() < 2) {
        return;
    }

    if (activeWindowChooser != nullptr) {
        WindowChooser *previous = activeWindowChooser;
        activeWindowChooser = nullptr;
        activeWindowChooserLabel.clear();
        previous->hide();
        previous->deleteLater();
    }

    QRect bounds = screen_;

    if (QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()))) {
        bounds = screen->geometry();
    } else if (QScreen *primary = QGuiApplication::primaryScreen()) {
        bounds = primary->geometry();
    }

    auto *chooser = new WindowChooser(windows, entry.icon, bounds);
    activeWindowChooser = chooser;
    activeWindowChooserLabel = entry.label;
    chooser->openAnimated();
}

bool DockBar::activateEntry(int index)
{
    if (index < 0 || index >= entries_.size()) {
        return false;
    }

    const Entry &entry = entries_.at(index);

    if (EdenDesktop::activateMatching(
            entry.matchClasses,
            entry.pids,
            entry.matchTitles)) {
        return true;
    }

    if (entry.label == QStringLiteral("Ajustes")) {
        if (SettingsWindow *settings = SettingsWindow::instance()) {
            if (settings->isMinimized()) {
                settings->showNormal();
            }

            settings->show();
            settings->raise();
            settings->activateWindow();
            return true;
        }
    }

    return false;
}

void DockBar::activateOrLaunch(int index)
{
    if (index < 0 || index >= entries_.size()) {
        return;
    }

    Entry &entry = entries_[index];

    if (entry.desktopMenu || entry.buildsDesktop || entry.separator) {
        return;
    }

    EdenNotify::clear(entry.label, entry.iconName);

    if (openWindowCount(index) >= 2) {
        showWindowChooser(index);
        entry.launched = true;
        entry.loading = false;
        entry.openWindows = openWindowCount(index);
        update();
        return;
    }

    if (activeWindowChooser != nullptr) {
        activeWindowChooser->dismiss();
    }

    if (activateEntry(index)) {
        entry.launched = true;
        entry.loading = false;
        entry.openWindows = std::max(1, openWindowCount(index));
        update();
        return;
    }

    if (entry.launch != nullptr) {
        if (entry.launch()) {
            beginLoading(index);
        }

        return;
    }

    if (entry.launchArgs.isEmpty()) {
        return;
    }

    qint64 pid = 0;

    if (EdenDesktop::launchCommand(entry.launchArgs, entry.terminal, &pid)) {
        rememberPid(index, pid);
        beginLoading(index);
    }
}

bool DockBar::canManageDockPresence(int index) const
{
    if (index < 0 || index >= entries_.size() || index >= separatorIndex()) {
        return false;
    }

    const Entry &entry = entries_.at(index);
    return !entry.separator && !entry.desktopMenu && !entry.buildsDesktop;
}

bool DockBar::entryLooksOpen(int index) const
{
    if (index < 0 || index >= entries_.size()) {
        return false;
    }

    const Entry &entry = entries_.at(index);
    return entry.openWindows > 0 || entry.launched || entry.loading || !entry.pids.isEmpty();
}

void DockBar::savePinnedApps()
{
    QStringList labels;
    QSet<QString> keepCustom;

    for (const Entry &entry : entries_) {
        if (entry.separator) {
            break;
        }

        if (entry.ephemeral || entry.desktopMenu || entry.buildsDesktop) {
            continue;
        }

        labels.push_back(entry.label);

        if (entry.builtin || entry.launch != nullptr) {
            continue;
        }

        CustomDockApp app;
        app.label = entry.label;
        app.iconName = entry.iconName;
        app.launchArgs = entry.launchArgs;
        app.matchClasses = entry.matchClasses;
        app.matchTitles = entry.matchTitles;
        app.terminal = entry.terminal;

        if (app.iconName.isEmpty() && !entry.icon.isNull()) {
            app.iconName = cacheDockIcon(entry.label, entry.icon);
        }

        saveCustomDockApp(app);
        keepCustom.insert(sanitizeDockKey(entry.label));
    }

    savePinnedLabels(labels);

    const QDir apps(dockAppsDir());
    const QFileInfoList files = apps.entryInfoList(
        {QStringLiteral("*.app")},
        QDir::Files);

    for (const QFileInfo &info : files) {
        if (keepCustom.contains(info.completeBaseName())) {
            continue;
        }

        QFile::remove(info.absoluteFilePath());
        QFile::remove(QDir(dockIconsDir()).filePath(
            info.completeBaseName() + QStringLiteral(".png")));
    }
}

void DockBar::restoreBuiltinHooks(Entry &entry)
{
    struct Hook
    {
        const char *label;
        bool (*launch)();
        const char *icon;
    };

    static const Hook hooks[] = {
        {"Arquivos", launchFiles, "eden-launcher.svg"},
        {"Terminal", launchTerminal, "eden-terminal.svg"},
        {"Editor", launchIde, "eden-ide.svg"},
        {"Git", launchGit, "eden-git.svg"},
        {"Docker", launchDocker, "eden-docker.svg"},
        {"Banco", launchDatabase, "eden-database.svg"},
        {"API", launchApi, "eden-api.svg"},
        {"Pacotes", launchPackage, "eden-package.svg"},
        {"Depurador", launchDebugger, "eden-debugger.svg"}
    };

    for (const Hook &hook : hooks) {
        if (entry.label.compare(QString::fromUtf8(hook.label), Qt::CaseInsensitive) != 0) {
            continue;
        }

        entry.launch = hook.launch;
        entry.builtin = true;
        entry.iconName = QString::fromUtf8(hook.icon);

        if (entry.icon.isNull()) {
            entry.icon = loadIcon(entry.iconName);
        }

        return;
    }
}

void DockBar::keepInDock(int index)
{
    if (!canManageDockPresence(index)) {
        return;
    }

    Entry &entry = entries_[index];

    if (!entry.ephemeral) {
        return;
    }

    restoreBuiltinHooks(entry);
    entry.ephemeral = false;

    if (!entry.builtin && entry.launch == nullptr && entry.iconName.isEmpty() &&
        !entry.icon.isNull()) {
        entry.iconName = cacheDockIcon(entry.label, entry.icon);
    }

    savePinnedApps();
    update();
}

void DockBar::removeFromDock(int index)
{
    if (!canManageDockPresence(index)) {
        return;
    }

    Entry &entry = entries_[index];

    if (entry.ephemeral) {
        return;
    }

    if (entryLooksOpen(index)) {
        entry.ephemeral = true;
        savePinnedApps();
        update();
        return;
    }

    const QString label = entry.label;
    const bool custom = !entry.builtin && entry.launch == nullptr;
    entries_.removeAt(index);

    if (hover_ == index) {
        hover_ = -1;
    } else if (hover_ > index) {
        --hover_;
    }

    if (custom) {
        removeCustomDockApp(label);
    }

    savePinnedApps();
    relayout();
}

void DockBar::noteClosed(const QString &label)
{
    const int index = findEntry(label);

    if (index < 0) {
        return;
    }

    Entry &entry = entries_[index];
    entry.loading = false;
    entry.launched = false;
    entry.openWindows = 0;
    entry.pids.clear();

    if (!anyLoading()) {
        loadingTimer_->stop();
    }

    if (entry.ephemeral) {
        entries_.removeAt(index);

        if (hover_ == index) {
            hover_ = -1;
        } else if (hover_ > index) {
            --hover_;
        }

        relayout();
        return;
    }

    updateMask();
    update();
}

void DockBar::closeEntry(int index)
{
    if (index < 0 || index >= entries_.size()) {
        return;
    }

    Entry &entry = entries_[index];

    if (!entry.closeable) {
        return;
    }

    if (entry.label == QStringLiteral("Gerenciador do Sistema")) {
        if (SystemManager *manager = SystemManager::instance()) {
            manager->dismiss();
        } else {
            noteClosed(QStringLiteral("Gerenciador do Sistema"));
        }

        return;
    }

    bool closed = false;

    if (entry.label == QStringLiteral("Ajustes")) {
        if (SettingsWindow *settings = SettingsWindow::instance()) {
            settings->close();
            closed = true;
        }
    }

    closed = EdenDesktop::closeMatching(
                 entry.matchClasses,
                 entry.pids,
                 entry.matchTitles) ||
             closed;

    if (!closed) {
        return;
    }

    entry.loading = false;
    entry.launched = false;
    entry.openWindows = 0;
    entry.pids.clear();

    if (!anyLoading()) {
        loadingTimer_->stop();
    }

    if (entry.ephemeral) {
        entries_.removeAt(index);
        hover_ = -1;
        relayout();
        return;
    }

    updateMask();
    update();
}

void DockBar::showEntryMenu(int index)
{
    if (index < 0 || index >= entries_.size()) {
        return;
    }

    const Entry &entry = entries_.at(index);

    if (entry.separator || entry.desktopMenu || entry.buildsDesktop) {
        return;
    }

    const QString label = entry.label;
    const bool manage = canManageDockPresence(index);
    const bool open = entryLooksOpen(index);
    QVector<DockPopup::Row> rows;
    rows.push_back({label, false, false});

    int keepIndex = -1;
    int removeIndex = -1;
    int closeIndex = -1;

    if (manage && entry.ephemeral) {
        keepIndex = rows.size();
        rows.push_back({QStringLiteral("Manter no Dock"), true, false});
    }

    if (manage && !entry.ephemeral) {
        removeIndex = rows.size();
        rows.push_back({QStringLiteral("Remover do Dock"), true, false});
    }

    if (entry.closeable && open) {
        closeIndex = rows.size();
        rows.push_back({QStringLiteral("Fechar"), true, false});
    }

    if (rows.size() == 1) {
        return;
    }

    hover_ = -1;
    relayout();
    const QPoint anchor = mapToGlobal(
        QPoint(iconRects_.value(index).center().x(), barRect_.top()));
    const int chosen = runDockPopup(rows, anchor, screen_);
    const int current = findEntry(label);

    if (current < 0) {
        return;
    }

    if (chosen == keepIndex) {
        keepInDock(current);
        return;
    }

    if (chosen == removeIndex) {
        removeFromDock(current);
        return;
    }

    if (chosen == closeIndex) {
        closeEntry(current);
    }
}

void DockBar::mousePressEvent(QMouseEvent *event)
{
    const int index = entryAt(event->position().toPoint());

    if (index < 0) {
        return;
    }

    if (event->button() == Qt::RightButton) {
        showEntryMenu(index);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (entries_.at(index).desktopMenu || entries_.at(index).buildsDesktop) {
        return;
    }

    activateOrLaunch(index);
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
    QVector<DockPopup::Row> rows;
    rows.reserve(4);

    for (int desktop = 0; desktop < 4; ++desktop) {
        rows.push_back({
            QStringLiteral("Área de trabalho %1").arg(desktop + 1),
            true,
            static_cast<uint32_t>(desktop) == active
        });
    }

    hover_ = -1;
    relayout();
    const QPoint anchor = mapToGlobal(
        QPoint(iconRects_.value(index).center().x(), barRect_.top()));
    const int chosen = runDockPopup(rows, anchor, screen_);

    if (chosen < 0 || chosen >= 4 || index < 0 || index >= entries_.size()) {
        return;
    }

    requestDesktop(static_cast<uint32_t>(chosen));
    entries_[index].launched = true;
    update();
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
        QStringLiteral("edens"),
        QStringLiteral("eden-installer"),
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

    if (!QFileInfo::exists(windowManager) || !QFileInfo::exists(desktop)) {
        showBuildAbort(QStringLiteral(
            "A compilação foi válida, mas o binário novo não foi encontrado."));
        return;
    }

    // Drop the old WM cleanly and wait until SubstructureRedirect is free,
    // otherwise the new manager exits and nothing can open windows.
    const QVector<qint64> stopped = stopDesktopTools();

    if (!waitForProcessesGone(stopped, 2500)) {
        showBuildAbort(QStringLiteral(
            "A compilação foi válida, mas o gerenciador antigo não encerrou a tempo."));
        return;
    }

    if (!relaunchWindowManager(windowManager)) {
        showBuildAbort(QStringLiteral(
            "A compilação foi válida, mas o Eden não conseguiu reabrir o gerenciador. A sessão atual foi mantida."));
        return;
    }

    // Replace this process with the new desktop. The display manager keeps
    // waiting on the same PID, so the login screen does not come back.
    if (!relaunchDesktopInPlace(desktop)) {
        showBuildAbort(QStringLiteral(
            "A compilação foi válida, mas o Eden não conseguiu recarregar o desktop. A sessão atual foi mantida."));
    }
}
