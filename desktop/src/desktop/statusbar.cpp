#include "desktop/statusbar.hpp"
#include "desktop/desktopwindow.hpp"
#include "desktop/glassstyle.hpp"
#include "desktop/networkmenu.hpp"
#include "desktop/sysmetrics.hpp"
#include "desktop/wallpapersettings.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QFile>
#include <QFileDevice>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QLineEdit>
#include <QLocale>
#include <QBitmap>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QStandardPaths>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <cstring>
#include <functional>
#include <sys/select.h>

namespace {

QString sessionPath()
{
    const QString directory = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("git-session"));
}

QString gitConfig(const QString &key)
{
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("--get"), key});

    if (!process.waitForFinished(3000) || process.exitCode() != 0) {
        return {};
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool runGit(const QStringList &arguments, const QByteArray &input, QString &output, QString &error)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));

    if (git.isEmpty()) {
        error = QStringLiteral("O git não está instalado.");
        return false;
    }

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    process.setProcessEnvironment(environment);
    process.start(git, arguments);

    if (!process.waitForStarted(3000)) {
        error = QStringLiteral("Não consegui iniciar o git.");
        return false;
    }

    if (!input.isEmpty()) {
        process.write(input);
    }

    process.closeWriteChannel();

    if (!process.waitForFinished(8000)) {
        process.kill();
        error = QStringLiteral("O git demorou para responder.");
        return false;
    }

    output = QString::fromUtf8(process.readAllStandardOutput());
    const QString gitError = QString::fromUtf8(process.readAllStandardError()).trimmed();

    if (process.exitCode() != 0) {
        error = gitError.isEmpty() ? QStringLiteral("O git recusou o comando.") : gitError;
        return false;
    }

    return true;
}

QString normalizeHost(QString host)
{
    host = host.trimmed().toLower();

    if (host.startsWith(QStringLiteral("https://"))) {
        host.remove(0, 8);
    } else if (host.startsWith(QStringLiteral("http://"))) {
        host.remove(0, 7);
    }

    const int slash = host.indexOf(QLatin1Char('/'));

    if (slash >= 0) {
        host.truncate(slash);
    }

    return host;
}

QString extractToken(const QString &text);

bool fieldIsSafe(const QString &value)
{
    return !value.contains(QLatin1Char('\n')) &&
           !value.contains(QLatin1Char('\r')) &&
           !value.contains(QLatin1Char('"'));
}

struct GitAccount
{
    QString host;
    QString username;
    QString name;
    QString email;
    bool active = false;
};

GitAccount readSessionFile()
{
    GitAccount account;
    QFile file(sessionPath());

    if (!file.open(QIODevice::ReadOnly)) {
        return account;
    }

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        const int split = line.indexOf(QLatin1Char('='));

        if (split <= 0) {
            continue;
        }

        const QString key = line.left(split);
        const QString value = line.mid(split + 1);

        if (key == QStringLiteral("host")) {
            account.host = value;
        } else if (key == QStringLiteral("username")) {
            account.username = value;
        } else if (key == QStringLiteral("name")) {
            account.name = value;
        } else if (key == QStringLiteral("email")) {
            account.email = value;
        }
    }

    return account;
}

bool writeSessionFile(const GitAccount &account)
{
    QFile file(sessionPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const QByteArray body =
        "host=" + account.host.toUtf8() + "\n"
        "username=" + account.username.toUtf8() + "\n"
        "name=" + account.name.toUtf8() + "\n"
        "email=" + account.email.toUtf8() + "\n";
    const bool wrote = file.write(body) == body.size();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return wrote;
}

QByteArray credentialDescription(const QString &host, const QString &username, const QString &secret)
{
    QByteArray body;
    body += "protocol=https\n";
    body += "host=" + host.toUtf8() + "\n";
    body += "username=" + username.toUtf8() + "\n";

    if (!secret.isEmpty()) {
        body += "password=" + secret.toUtf8() + "\n";
    }

    body += "\n";
    return body;
}

bool credentialContainsSecret(const QString &host, const QString &username)
{
    QString output;
    QString error;
    const bool ok = runGit(
        {QStringLiteral("credential"), QStringLiteral("fill")},
        credentialDescription(host, username, {}),
        output,
        error);

    const bool found = ok && output.contains(QStringLiteral("password="));
    output.fill(QLatin1Char('x'));
    return found;
}

QString sessionSecret(const QString &host, const QString &username)
{
    QString output;
    QString error;
    const bool ok = runGit(
        {QStringLiteral("credential"), QStringLiteral("fill")},
        credentialDescription(host, username, {}),
        output,
        error);
    QString secret;

    if (ok) {
        const QStringList lines = output.split(QLatin1Char('\n'));

        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("password="))) {
                secret = line.mid(9);
                break;
            }
        }
    }

    output.fill(QLatin1Char('x'));
    return secret;
}

bool ensureCredentialHelper(QString &error)
{
    const QString helper = gitConfig(QStringLiteral("credential.helper"));

    if (!helper.isEmpty()) {
        return true;
    }

    QString output;
    return runGit(
        {QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("credential.helper"), QStringLiteral("store")},
        {},
        output,
        error);
}

QString remoteLogin(const QString &host, const QString &token, QString &error)
{
    const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl"));

    if (curl.isEmpty()) {
        error = QStringLiteral("O curl não está instalado para validar o login.");
        return {};
    }

    QString url;
    QByteArray header;

    if (host == QStringLiteral("github.com")) {
        url = QStringLiteral("https://api.github.com/user");
        header = "header = \"Authorization: Bearer " + token.toUtf8() + "\"\n"
                 "header = \"Accept: application/vnd.github+json\"\n"
                 "header = \"User-Agent: Eden-desktop\"\n";
    } else if (host == QStringLiteral("gitlab.com")) {
        url = QStringLiteral("https://gitlab.com/api/v4/user");
        header = "header = \"PRIVATE-TOKEN: " + token.toUtf8() + "\"\n"
                 "header = \"User-Agent: Eden-desktop\"\n";
    } else {
        return QStringLiteral("*");
    }

    QProcess process;
    process.start(curl, {
        QStringLiteral("-sS"),
        QStringLiteral("--max-time"),
        QStringLiteral("12"),
        QStringLiteral("-o"),
        QStringLiteral("-"),
        QStringLiteral("-w"),
        QStringLiteral("\n%{http_code}"),
        QStringLiteral("--config"),
        QStringLiteral("-"),
        url
    });

    if (!process.waitForStarted(3000)) {
        error = QStringLiteral("Não consegui consultar o servidor do Git.");
        return {};
    }

    process.write(header);
    process.closeWriteChannel();

    if (!process.waitForFinished(15000)) {
        process.kill();
        error = QStringLiteral("O servidor do Git demorou para responder.");
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const int split = output.lastIndexOf(QLatin1Char('\n'));
    const int status = output.mid(split + 1).trimmed().toInt();
    const QByteArray body = split < 0 ? QByteArray() : output.left(split).toUtf8();

    if (status == 401 || status == 403) {
        error = QStringLiteral(
            "O GitHub recusou esse token. Use o código que começa com ghp_, gerado em github.com/settings/tokens. O número de 6 dígitos do autenticador não serve.");
        return {};
    }

    if (status != 200) {
        error = QStringLiteral("Não consegui validar o login (resposta %1).").arg(status);
        return {};
    }

    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const QString login = json.value(QStringLiteral("login")).toString(
        json.value(QStringLiteral("username")).toString());

    if (login.isEmpty()) {
        error = QStringLiteral("O servidor não confirmou a conta.");
        return {};
    }

    return login;
}

GitAccount currentAccount()
{
    GitAccount account = readSessionFile();

    if (account.host.isEmpty() || account.username.isEmpty()) {
        return {};
    }

    account.active = credentialContainsSecret(account.host, account.username);
    return account;
}

bool loginAccount(
    QString host,
    const QString &username,
    const QString &token,
    const QString &name,
    const QString &email,
    QString &error)
{
    host = normalizeHost(host);

    const QString secret = extractToken(token);
    const QString trimmedToken = token.trimmed();

    if (host.isEmpty() || secret.isEmpty()) {
        if (QRegularExpression(QStringLiteral("^\\d{6,8}$")).match(trimmedToken).hasMatch()) {
            error = QStringLiteral(
                "Esse é o código do autenticador. O Eden precisa do token do GitHub que começa com ghp_.");
        } else {
            error = QStringLiteral(
                "Não reconheci um token. Copie o código que começa com ghp_ ou github_pat_.");
        }

        return false;
    }

    if (!fieldIsSafe(host) || !fieldIsSafe(secret)) {
        error = QStringLiteral("O token tem um caractere que não posso enviar.");
        return false;
    }

    const QString confirmed = remoteLogin(host, secret, error);

    if (confirmed.isEmpty()) {
        return false;
    }

    const QString accountName = confirmed == QStringLiteral("*") ? username.trimmed() : confirmed;

    if (!ensureCredentialHelper(error)) {
        return false;
    }

    QString output;

    if (!runGit(
            {QStringLiteral("credential"), QStringLiteral("approve")},
            credentialDescription(host, accountName, secret),
            output,
            error)) {
        return false;
    }

    if (!name.trimmed().isEmpty()) {
        runGit(
            {QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("user.name"), name.trimmed()},
            {},
            output,
            error);
    }

    if (!email.trimmed().isEmpty()) {
        runGit(
            {QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("user.email"), email.trimmed()},
            {},
            output,
            error);
    }

    GitAccount account;
    account.host = host;
    account.username = accountName;
    account.name = name.trimmed();
    account.email = email.trimmed();

    if (!writeSessionFile(account)) {
        error = QStringLiteral("O login foi aceito, mas não consegui gravar a sessão.");
        return false;
    }

    return true;
}

bool logoutAccount(QString &error)
{
    const GitAccount account = readSessionFile();

    if (!account.host.isEmpty() && !account.username.isEmpty()) {
        QString output;
        runGit(
            {QStringLiteral("credential"), QStringLiteral("reject")},
            credentialDescription(account.host, account.username, {}),
            output,
            error);
    }

    QFile::remove(sessionPath());
    return true;
}

QString extractToken(const QString &text)
{
    const QRegularExpression pattern(QStringLiteral(
        "(github_pat_[A-Za-z0-9_]{20,}|ghp_[A-Za-z0-9]{20,}|gho_[A-Za-z0-9]{20,}|glpat-[A-Za-z0-9_\\-]{20,})"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

xcb_generic_event_t *waitForXcbEvent(xcb_connection_t *connection)
{
    if (xcb_generic_event_t *queued = xcb_poll_for_event(connection)) {
        return queued;
    }

    const int descriptor = xcb_get_file_descriptor(connection);
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(descriptor, &ready);
    timeval wait{0, 80000};

    if (select(descriptor + 1, &ready, nullptr, nullptr, &wait) <= 0) {
        return nullptr;
    }

    return xcb_poll_for_event(connection);
}

QString readSelection(
    xcb_connection_t *connection,
    xcb_window_t window,
    xcb_atom_t selection,
    xcb_atom_t target,
    xcb_atom_t property,
    xcb_timestamp_t time)
{
    xcb_delete_property(connection, window, property);
    xcb_convert_selection(connection, window, selection, target, property, time);
    xcb_flush(connection);

    for (int attempt = 0; attempt < 25; ++attempt) {
        xcb_generic_event_t *event = waitForXcbEvent(connection);

        if (event == nullptr) {
            break;
        }

        const uint8_t type = event->response_type & 0x7f;
        QString text;

        if (type == XCB_SELECTION_NOTIFY) {
            const auto *notify = reinterpret_cast<xcb_selection_notify_event_t *>(event);

            if (notify->property != XCB_NONE) {
                const auto cookie = xcb_get_property(
                    connection,
                    1,
                    window,
                    property,
                    XCB_GET_PROPERTY_TYPE_ANY,
                    0,
                    2048);
                xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);

                if (reply != nullptr) {
                    const int length = xcb_get_property_value_length(reply);
                    text = QString::fromUtf8(
                        static_cast<const char *>(xcb_get_property_value(reply)),
                        length);
                    std::free(reply);
                }
            }
        }

        std::free(event);

        if (type == XCB_SELECTION_NOTIFY) {
            return text.trimmed();
        }
    }

    return {};
}

QString readClipboardOnDisplay(const QByteArray &displayName)
{
    int preferredScreen = 0;
    xcb_connection_t *connection = xcb_connect(displayName.constData(), &preferredScreen);

    if (connection == nullptr || xcb_connection_has_error(connection) != 0) {
        if (connection != nullptr) {
            xcb_disconnect(connection);
        }

        return {};
    }

    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;

    if (screen == nullptr) {
        xcb_disconnect(connection);
        return {};
    }

    const xcb_window_t window = xcb_generate_id(connection);
    const uint32_t values[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};

    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        -10,
        -10,
        1,
        1,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        XCB_CW_EVENT_MASK,
        values);
    xcb_map_window(connection, window);

    const auto atom = [connection](const char *name) {
        const auto cookie = xcb_intern_atom(
            connection,
            0,
            static_cast<uint16_t>(std::strlen(name)),
            name);
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);

        if (reply == nullptr) {
            return static_cast<xcb_atom_t>(XCB_ATOM_NONE);
        }

        const xcb_atom_t value = reply->atom;
        std::free(reply);
        return value;
    };

    const xcb_atom_t clipboard = atom("CLIPBOARD");
    const xcb_atom_t utf8 = atom("UTF8_STRING");
    const xcb_atom_t property = atom("EDEN_PASTE");
    xcb_timestamp_t timestamp = XCB_CURRENT_TIME;
    const char marker[] = "e";

    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE,
        window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        1,
        marker);
    xcb_flush(connection);

    for (int attempt = 0; attempt < 15; ++attempt) {
        xcb_generic_event_t *event = waitForXcbEvent(connection);

        if (event == nullptr) {
            break;
        }

        const uint8_t type = event->response_type & 0x7f;

        if (type == XCB_PROPERTY_NOTIFY) {
            timestamp = reinterpret_cast<xcb_property_notify_event_t *>(event)->time;
            std::free(event);
            break;
        }

        std::free(event);
    }

    QString text;

    if (clipboard != XCB_ATOM_NONE && utf8 != XCB_ATOM_NONE && property != XCB_ATOM_NONE) {
        text = readSelection(connection, window, clipboard, utf8, property, timestamp);

        if (extractToken(text).isEmpty()) {
            const QString primary = readSelection(
                connection,
                window,
                XCB_ATOM_PRIMARY,
                utf8,
                property,
                timestamp);

            if (extractToken(primary).isEmpty() == false || text.isEmpty()) {
                text = primary.isEmpty() ? text : primary;
            }
        }
    }

    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
    return text.trimmed();
}

QString copiedText()
{
    const QString clipboard = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    const QString selection = QGuiApplication::clipboard()->text(QClipboard::Selection);

    if (!extractToken(clipboard).isEmpty()) {
        return clipboard;
    }

    if (!extractToken(selection).isEmpty()) {
        return selection;
    }

    const QByteArray display = qgetenv("DISPLAY");
    const bool nested = !display.isEmpty() && display != ":0" && !display.startsWith(":0.");

    if (nested) {
        const QString hostClipboard = readClipboardOnDisplay(QByteArrayLiteral(":0"));

        if (!hostClipboard.isEmpty()) {
            return hostClipboard;
        }
    }

    if (!clipboard.trimmed().isEmpty()) {
        return clipboard;
    }

    return selection;
}

QColor trayInk()
{
    return EdenGlass::current().textColor();
}

QPixmap gitIcon(bool active)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(trayInk(), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(5, 4), 2.1, 2.1);
    painter.drawEllipse(QPointF(5, 14), 2.1, 2.1);
    painter.drawEllipse(QPointF(13, 9), 2.1, 2.1);
    painter.drawLine(QPointF(5, 6.1), QPointF(5, 11.9));
    painter.drawLine(QPointF(5, 12), QPointF(11.2, 9.4));

    if (active) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#00E5FF")));
        painter.drawEllipse(QPointF(15, 3.2), 2.5, 2.5);
    }

    return pixmap;
}

QPixmap powerIcon()
{
    // Clean SF-style power glyph for the status bar.
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(trayInk(), 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(3.0, 4.2, 12.0, 12.0), 52 * 16, 276 * 16);
    painter.drawLine(QPointF(9.0, 2.4), QPointF(9.0, 9.0));
    return pixmap;
}

QPixmap powerMenuGlyph(const QString &kind)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor fill(0xF2, 0xF2, 0xF7);
    QColor ink(0x1C, 0x1C, 0x1E);

    if (kind == QLatin1String("shutdown")) {
        fill = QColor(0xFF, 0xEB, 0xEE);
        ink = QColor(0xFF, 0x3B, 0x30);
    } else if (kind == QLatin1String("reboot")) {
        fill = QColor(0x12, 0x2A, 0x32);
        ink = QColor(0x00, 0xE5, 0xFF);
    } else if (kind == QLatin1String("logout")) {
        fill = QColor(0x1A, 0x24, 0x2E);
        ink = QColor(0xE7, 0xF2, 0xF6);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(0.5, 0.5, 27, 27));

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    if (kind == QLatin1String("shutdown")) {
        painter.drawArc(QRectF(7.2, 8.0, 13.6, 13.6), 50 * 16, 280 * 16);
        painter.drawLine(QPointF(14, 6.2), QPointF(14, 13.2));
    } else if (kind == QLatin1String("reboot")) {
        painter.drawArc(QRectF(7.5, 7.5, 13.0, 13.0), 40 * 16, 260 * 16);
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        QPolygonF tip;
        tip << QPointF(18.8, 8.0) << QPointF(22.2, 11.6) << QPointF(16.6, 12.0);
        painter.drawPolygon(tip);
    } else {
        painter.drawRoundedRect(QRectF(7.5, 8.0, 9.5, 12.0), 2.2, 2.2);
        painter.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(14.5, 14.0), QPointF(21.0, 14.0));
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        QPolygonF tip;
        tip << QPointF(18.2, 11.0) << QPointF(21.8, 14.0) << QPointF(18.2, 17.0);
        painter.drawPolygon(tip);
    }

    return pixmap;
}

class PowerMenu final : public QWidget
{
public:
    explicit PowerMenu(QWidget *anchor)
        : QWidget(nullptr)
        , anchor_(anchor)
    {
        setWindowFlags(
            Qt::Popup |
            Qt::FramelessWindowHint |
            Qt::NoDropShadowWindowHint |
            Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(220);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        layout->addWidget(makeRow(
            QStringLiteral("logout"),
            QStringLiteral("Encerrar sessão"),
            QStringLiteral("Sair do Eden")));
        layout->addWidget(makeRow(
            QStringLiteral("reboot"),
            QStringLiteral("Reiniciar"),
            QStringLiteral("Reinicia o computador")));
        layout->addWidget(makeRow(
            QStringLiteral("shutdown"),
            QStringLiteral("Desligar"),
            QStringLiteral("Desliga o computador")));

        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        // Mask applied after show/adjust in open().
    }

    void open()
    {
        adjustSize();
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        setMask(QRegion(path.toFillPolygon().toPolygon()));

        QRect screen(0, 0, 1280, 800);

        if (anchor_ != nullptr && anchor_->screen() != nullptr) {
            screen = anchor_->screen()->geometry();
        }

        const QPoint bottomRight =
            anchor_->mapToGlobal(QPoint(anchor_->width(), anchor_->height()));
        int x = bottomRight.x() - width();
        int y = bottomRight.y() + 6;
        x = qBound(screen.left() + 8, x, screen.right() - width() - 8);

        if (y + height() > screen.bottom() - 8) {
            y = bottomRight.y() - height() - 6;
        }

        move(x, y);
        show();
        raise();
    }

    std::function<void(const QString &)> onChosen;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(255, 255, 255, 38), 1));
        painter.setBrush(QColor(18, 24, 32, 230));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);
    }

private:
    QWidget *makeRow(const QString &id, const QString &title, const QString &subtitle)
    {
        auto *button = new QPushButton(this);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        button->setMinimumHeight(52);
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: transparent; border: none; border-radius: 10px; text-align: left;"
            "}"
            "QPushButton:hover { background: rgba(0,229,255,0.16); }"));

        auto *row = new QHBoxLayout(button);
        row->setContentsMargins(8, 6, 10, 6);
        row->setSpacing(10);

        auto *icon = new QLabel(button);
        icon->setFixedSize(28, 28);
        icon->setPixmap(powerMenuGlyph(id));
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(icon);

        auto *text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(1);

        auto *name = new QLabel(title, button);
        QFont nameFont = name->font();
        nameFont.setPointSize(13);
        nameFont.setWeight(QFont::DemiBold);
        name->setFont(nameFont);
        name->setStyleSheet(
            id == QLatin1String("shutdown")
                ? QStringLiteral("color: #FF3B30; background: transparent;")
                : QStringLiteral("color: #E7F2F6; background: transparent;"));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *detail = new QLabel(subtitle, button);
        detail->setStyleSheet(QStringLiteral("color: #8FA3B3; background: transparent; font-size: 11px;"));
        detail->setAttribute(Qt::WA_TransparentForMouseEvents);

        text->addWidget(name);
        text->addWidget(detail);
        row->addLayout(text, 1);

        connect(button, &QPushButton::clicked, this, [this, id]() {
            choice_ = id;
            close();

            if (onChosen) {
                onChosen(id);
            }
        });

        return button;
    }

    QWidget *anchor_ = nullptr;
    QString choice_;
};

QPixmap networkIcon()
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(trayInk(), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(2.2, 5.5, 13.6, 13.6), 40 * 16, 100 * 16);
    painter.drawArc(QRectF(4.4, 7.8, 9.2, 9.2), 40 * 16, 100 * 16);
    painter.setBrush(QColor(QStringLiteral("#00E5FF")));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(9, 14.2), 1.55, 1.55);
    return pixmap;
}

bool confirmPower(const QString &title, const QString &question)
{
    return QMessageBox::question(
               nullptr,
               title,
               question,
               QMessageBox::Yes | QMessageBox::Cancel,
               QMessageBox::Cancel) == QMessageBox::Yes;
}

bool startSystemCommand(const QString &program, const QStringList &arguments)
{
    const QString executable = QStandardPaths::findExecutable(program);

    if (executable.isEmpty()) {
        return false;
    }

    return QProcess::startDetached(executable, arguments);
}

bool powerOffMachine()
{
    return startSystemCommand(QStringLiteral("loginctl"), {QStringLiteral("poweroff")}) ||
           startSystemCommand(QStringLiteral("systemctl"), {QStringLiteral("poweroff")}) ||
           startSystemCommand(QStringLiteral("shutdown"), {QStringLiteral("-h"), QStringLiteral("now")});
}

bool rebootMachine()
{
    return startSystemCommand(QStringLiteral("loginctl"), {QStringLiteral("reboot")}) ||
           startSystemCommand(QStringLiteral("systemctl"), {QStringLiteral("reboot")}) ||
           startSystemCommand(QStringLiteral("shutdown"), {QStringLiteral("-r"), QStringLiteral("now")});
}

bool endLoginSession()
{
    DesktopWindow::allowSessionExit();

    const QByteArray sessionId = qgetenv("XDG_SESSION_ID");

    if (!sessionId.isEmpty() &&
        startSystemCommand(
            QStringLiteral("loginctl"),
            {QStringLiteral("terminate-session"), QString::fromUtf8(sessionId)})) {
        QCoreApplication::quit();
        return true;
    }

    QCoreApplication::quit();
    return true;
}

bool popupIsBlockingRaise()
{
    return QApplication::activePopupWidget() != nullptr ||
           QApplication::activeModalWidget() != nullptr;
}

QPixmap sourceIcon()
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(trayInk(), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(8, 4), QPointF(3.5, 9));
    painter.drawLine(QPointF(3.5, 9), QPointF(8, 14));
    painter.drawLine(QPointF(10, 4), QPointF(14.5, 9));
    painter.drawLine(QPointF(14.5, 9), QPointF(10, 14));
    return pixmap;
}

QString edenProjectDir()
{
    const QDir binaryDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        QDir(binaryDir.absoluteFilePath(QStringLiteral(".."))).absolutePath(),
        binaryDir.absolutePath(),
        QDir::current().absolutePath(),
        QDir(QDir::current().absoluteFilePath(QStringLiteral(".."))).absolutePath()
    };

    for (const QString &candidate : candidates) {
        const QDir project(candidate);

        if (QFileInfo::exists(project.filePath(QStringLiteral("CMakeLists.txt"))) &&
            QFileInfo::exists(project.filePath(QStringLiteral("desktop/src/main.cpp")))) {
            return project.absolutePath();
        }
    }

    return {};
}

QString resolveEditor(const QString &name)
{
    const QString found = QStandardPaths::findExecutable(name);

    if (!found.isEmpty()) {
        return found;
    }

    const QStringList extras = {
        QDir::home().filePath(QStringLiteral(".local/bin/") + name),
        QStringLiteral("/usr/local/bin/") + name,
        QStringLiteral("/usr/bin/") + name,
        QStringLiteral("/snap/bin/") + name
    };

    for (const QString &path : extras) {
        if (QFileInfo::exists(path) && QFileInfo(path).isExecutable()) {
            return path;
        }
    }

    if (name == QStringLiteral("cursor") &&
        QFileInfo::exists(QStringLiteral("/usr/share/cursor/bin/cursor")) &&
        QFileInfo(QStringLiteral("/usr/share/cursor/bin/cursor")).isExecutable()) {
        return QStringLiteral("/usr/share/cursor/bin/cursor");
    }

    return {};
}

bool launchEditor(const QString &executable, const QStringList &arguments)
{
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        return false;
    }

    return QProcess::startDetached(executable, arguments);
}

bool openInEditor(const QString &folder)
{
    const struct {
        const char *name;
        QStringList arguments;
    } editors[] = {
        {"cursor", {QStringLiteral("-n"), folder}},
        {"cursor", {folder}},
        {"code", {QStringLiteral("-n"), folder}},
        {"code", {folder}},
        {"codium", {folder}},
        {"xed", {folder}},
        {"gnome-text-editor", {folder}},
        {"gedit", {folder}},
        {"mousepad", {folder}},
        {"kate", {folder}}
    };

    for (const auto &editor : editors) {
        if (launchEditor(resolveEditor(QString::fromLatin1(editor.name)), editor.arguments)) {
            return true;
        }
    }

    if (launchEditor(resolveEditor(QStringLiteral("xdg-open")), {folder})) {
        return true;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void showSourceWarning(const QString &text)
{
    QMessageBox box(
        QMessageBox::Warning,
        QStringLiteral("Source do desktop"),
        text);
    box.setWindowFlag(Qt::WindowStaysOnTopHint);
    box.exec();
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

void replaceAtom(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, xcb_atom_t value)
{
    if (property == XCB_ATOM_NONE || value == XCB_ATOM_NONE) {
        return;
    }

    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE,
        window,
        property,
        XCB_ATOM_ATOM,
        32,
        1,
        &value);
}

class GitLoginDialog final : public QDialog
{
public:
    explicit GitLoginDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Git"));
        setModal(true);
        setMinimumWidth(420);
        setStyleSheet(QStringLiteral(R"(
            QDialog { background: rgba(18, 24, 32, 0.96); }
            QLabel { color: #E7F2F6; }
            QLineEdit {
                background: rgba(255, 255, 255, 0.06);
                color: #E7F2F6;
                border: 1px solid rgba(255, 255, 255, 0.15);
                border-radius: 12px;
                padding: 7px 10px;
            }
            QPushButton {
                background: #00E5FF;
                color: #121820;
                border: none;
                border-radius: 12px;
                padding: 8px 16px;
                font-weight: 600;
            }
            QPushButton#secondary {
                background: rgba(255, 255, 255, 0.06);
                color: #E7F2F6;
                border: 1px solid rgba(255, 255, 255, 0.15);
            }
        )"));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 20, 22, 18);
        layout->setSpacing(12);

        title_ = new QLabel(QStringLiteral("Entrar no Git"));
        QFont titleFont = title_->font();
        titleFont.setPointSize(16);
        titleFont.setBold(true);
        title_->setFont(titleFont);
        layout->addWidget(title_);

        summary_ = new QLabel;
        summary_->setWordWrap(true);
        layout->addWidget(summary_);

        form_ = new QWidget;
        auto *formLayout = new QFormLayout(form_);
        formLayout->setSpacing(8);
        hostEdit_ = new QLineEdit(QStringLiteral("github.com"));
        userEdit_ = new QLineEdit;
        tokenEdit_ = new QLineEdit;
        tokenEdit_->setEchoMode(QLineEdit::Password);
        tokenEdit_->setPlaceholderText(QStringLiteral("cole o token aqui"));
        tokenEdit_->installEventFilter(this);
        nameEdit_ = new QLineEdit(gitConfig(QStringLiteral("user.name")));
        emailEdit_ = new QLineEdit(gitConfig(QStringLiteral("user.email")));

        auto *tokenRow = new QWidget;
        auto *tokenLayout = new QHBoxLayout(tokenRow);
        tokenLayout->setContentsMargins(0, 0, 0, 0);
        tokenLayout->setSpacing(8);
        auto *pasteButton = new QPushButton(QStringLiteral("Colar"));
        pasteButton->setObjectName(QStringLiteral("secondary"));
        tokenLayout->addWidget(tokenEdit_, 1);
        tokenLayout->addWidget(pasteButton);

        formLayout->addRow(QStringLiteral("Servidor"), hostEdit_);
        formLayout->addRow(QStringLiteral("Usuário (opcional)"), userEdit_);
        formLayout->addRow(QStringLiteral("Token"), tokenRow);
        formLayout->addRow(QStringLiteral("Nome"), nameEdit_);
        formLayout->addRow(QStringLiteral("E-mail"), emailEdit_);
        layout->addWidget(form_);

        error_ = new QLabel;
        error_->setWordWrap(true);
        error_->setStyleSheet(QStringLiteral("color: #C0392B;"));
        layout->addWidget(error_);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        secondary_ = new QPushButton(QStringLiteral("Cancelar"));
        secondary_->setObjectName(QStringLiteral("secondary"));
        primary_ = new QPushButton(QStringLiteral("Entrar"));
        buttons->addWidget(secondary_);
        buttons->addWidget(primary_);
        layout->addLayout(buttons);

        connect(secondary_, &QPushButton::clicked, this, &QDialog::reject);
        connect(primary_, &QPushButton::clicked, this, [this]() {
            submit();
        });
        connect(pasteButton, &QPushButton::clicked, this, [this]() {
            pasteToken();
        });

        showAccount(currentAccount());
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QDialog::showEvent(event);

        if (form_->isVisible()) {
            tokenEdit_->setFocus();
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == tokenEdit_ && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<const QKeyEvent *>(event);

            if (key->matches(QKeySequence::Paste)) {
                pasteToken();
                return true;
            }
        }

        return QDialog::eventFilter(watched, event);
    }

private:
    void pasteToken()
    {
        const QString raw = copiedText();
        const QString token = extractToken(raw);

        if (!token.isEmpty()) {
            tokenEdit_->setText(token);
            tokenEdit_->setFocus();
            error_->setStyleSheet(QStringLiteral("color: #1B7F3A;"));
            error_->setText(QStringLiteral("Token reconhecido. Clique em Entrar."));
            return;
        }

        error_->setStyleSheet(QStringLiteral("color: #C0392B;"));

        if (QRegularExpression(QStringLiteral("^\\d{6,8}$")).match(raw.trimmed()).hasMatch()) {
            error_->setText(QStringLiteral(
                "Isso é o código do autenticador. Copie o token do GitHub que começa com ghp_."));
            return;
        }

        error_->setText(raw.trimmed().isEmpty()
            ? QStringLiteral("Não encontrei texto copiado. No GitHub, clique em copiar ao lado do token ghp_ e depois em Colar.")
            : QStringLiteral("O texto copiado não é um token ghp_ ou github_pat_."));
    }

    void showAccount(const GitAccount &account)
    {
        const bool active = account.active;
        form_->setVisible(!active);
        error_->clear();

        if (active) {
            title_->setText(QStringLiteral("Sessão do Git"));
            summary_->setText(
                QStringLiteral("Você está em %1 como %2. A sessão fica guardada neste computador.")
                    .arg(account.host, account.username));
            primary_->setText(QStringLiteral("Encerrar sessão"));
            secondary_->setText(QStringLiteral("Fechar"));
            return;
        }

        title_->setText(QStringLiteral("Entrar no Git"));
        summary_->setText(QStringLiteral(
            "Use um token do GitHub ou do GitLab. Se o Ctrl+V não entrar, clique em Colar."));
        primary_->setText(QStringLiteral("Entrar"));
        secondary_->setText(QStringLiteral("Cancelar"));

        if (!account.host.isEmpty()) {
            hostEdit_->setText(account.host);
        }

        if (!account.username.isEmpty()) {
            userEdit_->setText(account.username);
        }
    }

    void submit()
    {
        error_->clear();
        const GitAccount account = currentAccount();

        if (account.active) {
            QString error;

            if (!logoutAccount(error)) {
                error_->setText(error);
                return;
            }

            showAccount({});
            accept();
            return;
        }

        primary_->setEnabled(false);
        QString error;
        const bool ok = loginAccount(
            hostEdit_->text(),
            userEdit_->text(),
            tokenEdit_->text(),
            nameEdit_->text(),
            emailEdit_->text(),
            error);
        tokenEdit_->clear();
        primary_->setEnabled(true);

        if (!ok) {
            error_->setText(error);
            return;
        }

        accept();
    }

    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *error_ = nullptr;
    QWidget *form_ = nullptr;
    QLineEdit *hostEdit_ = nullptr;
    QLineEdit *userEdit_ = nullptr;
    QLineEdit *tokenEdit_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *emailEdit_ = nullptr;
    QPushButton *primary_ = nullptr;
    QPushButton *secondary_ = nullptr;
};

struct Repo
{
    QString name;
    QString detail;
    QString url;
};

QList<Repo> parseRepos(const QString &host, const QByteArray &body)
{
    QList<Repo> repos;
    const QJsonArray list = QJsonDocument::fromJson(body).array();

    for (const QJsonValue &value : list) {
        const QJsonObject repo = value.toObject();
        Repo item;

        if (host == QStringLiteral("gitlab.com")) {
            item.name = repo.value(QStringLiteral("path_with_namespace")).toString();
            item.url = repo.value(QStringLiteral("web_url")).toString();
            item.detail = repo.value(QStringLiteral("visibility")).toString() == QStringLiteral("public")
                ? QStringLiteral("Público")
                : QStringLiteral("Privado");
        } else {
            item.name = repo.value(QStringLiteral("full_name")).toString();
            item.url = repo.value(QStringLiteral("html_url")).toString();
            item.detail = repo.value(QStringLiteral("private")).toBool()
                ? QStringLiteral("Privado")
                : QStringLiteral("Público");
        }

        const QString description = repo.value(QStringLiteral("description")).toString().simplified();

        if (!description.isEmpty()) {
            item.detail += QStringLiteral("  ·  ") + description.left(72);
        }

        if (!item.name.isEmpty()) {
            repos.append(item);
        }
    }

    return repos;
}

class RepoMenu final : public QWidget
{
public:
    explicit RepoMenu(const GitAccount &account, QWidget *anchor)
        : QWidget(nullptr)
        , account_(account)
        , anchor_(anchor)
    {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFixedWidth(340);
        setStyleSheet(QStringLiteral(R"(
            QScrollArea, QWidget#repoList { background: transparent; border: none; }
            QLabel { background: transparent; }
            QPushButton {
                background: transparent;
                border: none;
                border-radius: 8px;
                text-align: left;
            }
            QPushButton QLabel#repoName { color: #E7F2F6; font-size: 14px; }
            QPushButton QLabel#repoDetail { color: #8FA3B3; font-size: 12px; }
            QPushButton:hover { background: rgba(0,229,255,0.16); }
            QScrollBar:vertical {
                background: transparent;
                width: 8px;
                margin: 6px 2px;
            }
            QScrollBar::handle:vertical {
                background: rgba(0,229,255,0.45);
                border-radius: 4px;
                min-height: 24px;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        )"));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 12, 10, 10);
        layout->setSpacing(2);

        auto *title = new QLabel(account.username);
        QFont titleFont = title->font();
        titleFont.setPointSize(13);
        titleFont.setWeight(QFont::DemiBold);
        title->setFont(titleFont);
        title->setStyleSheet(QStringLiteral("color: #E7F2F6; padding: 2px 10px 0 10px;"));
        layout->addWidget(title);

        auto *caption = new QLabel(QStringLiteral("Repositórios"));
        caption->setStyleSheet(QStringLiteral("color: #8FA3B3; padding: 0 10px 6px 10px;"));
        layout->addWidget(caption);

        scroll_ = new QScrollArea;
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setMinimumHeight(420);
        scroll_->setMaximumHeight(560);

        list_ = new QWidget;
        list_->setObjectName(QStringLiteral("repoList"));
        listLayout_ = new QVBoxLayout(list_);
        listLayout_->setContentsMargins(0, 0, 0, 0);
        listLayout_->setSpacing(2);
        loading_ = new QLabel(QStringLiteral("Carregando…"));
        loading_->setStyleSheet(QStringLiteral("color: #8FA3B3; padding: 8px 10px;"));
        listLayout_->addWidget(loading_);
        listLayout_->addStretch();
        scroll_->viewport()->setAutoFillBackground(false);
        scroll_->setWidget(list_);
        layout->addWidget(scroll_);

        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: rgba(255,255,255,0.15); border: none;"));
        layout->addWidget(line);

        auto *logout = new QPushButton(QStringLiteral("Encerrar sessão"));
        logout->setCursor(Qt::PointingHandCursor);
        logout->setStyleSheet(QStringLiteral(
            "QPushButton { color: #E7F2F6; padding: 8px 10px; background: transparent; border: none; border-radius: 8px; }"
            "QPushButton:hover { background: rgba(0,229,255,0.16); }"));
        layout->addWidget(logout);
        connect(logout, &QPushButton::clicked, this, [this]() {
            QString error;
            logoutAccount(error);
            close();
        });
    }

    void open()
    {
        placeNearAnchor();
        show();
        raise();
        loadRepos();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);

        if (!plate_.isNull()) {
            painter.drawPixmap(0, 0, plate_);
            return;
        }

        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(255, 255, 255, 38)));
        painter.setBrush(QColor(18, 24, 32, 235));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kRadius, kRadius);
    }

private:
    void loadRepos()
    {
        QString secret = sessionSecret(account_.host, account_.username);

        if (secret.isEmpty()) {
            showMessage(QStringLiteral("A sessão expirou. Entre de novo."));
            return;
        }

        QString url;
        QByteArray header;

        if (account_.host == QStringLiteral("github.com")) {
            url = QStringLiteral("https://api.github.com/user/repos?per_page=100&sort=updated");
            header = "header = \"Authorization: Bearer " + secret.toUtf8() + "\"\n"
                     "header = \"Accept: application/vnd.github+json\"\n"
                     "header = \"User-Agent: Eden-desktop\"\n";
        } else if (account_.host == QStringLiteral("gitlab.com")) {
            url = QStringLiteral(
                "https://gitlab.com/api/v4/projects?membership=true&simple=true&order_by=last_activity_at&per_page=100");
            header = "header = \"PRIVATE-TOKEN: " + secret.toUtf8() + "\"\n"
                     "header = \"User-Agent: Eden-desktop\"\n";
        } else {
            secret.fill(QLatin1Char('x'));
            showMessage(QStringLiteral("Esta conta não lista repositórios daqui."));
            return;
        }

        secret.fill(QLatin1Char('x'));
        const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl"));

        if (curl.isEmpty()) {
            showMessage(QStringLiteral("O curl não está instalado."));
            return;
        }

        auto *process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput());
            process->deleteLater();
            const int split = output.lastIndexOf(QLatin1Char('\n'));
            const int status = output.mid(split + 1).trimmed().toInt();
            const QByteArray body = split < 0 ? QByteArray() : output.left(split).toUtf8();

            if (status == 401 || status == 403) {
                showMessage(QStringLiteral("A sessão não foi aceita. Entre de novo."));
                return;
            }

            if (status != 200) {
                showMessage(QStringLiteral("Não consegui carregar os repositórios."));
                return;
            }

            showRepos(parseRepos(account_.host, body));
        });
        process->start(curl, {
            QStringLiteral("-sS"),
            QStringLiteral("--max-time"),
            QStringLiteral("12"),
            QStringLiteral("-o"),
            QStringLiteral("-"),
            QStringLiteral("-w"),
            QStringLiteral("\n%{http_code}"),
            QStringLiteral("--config"),
            QStringLiteral("-"),
            url
        });

        if (!process->waitForStarted(3000)) {
            header.fill('x');
            showMessage(QStringLiteral("Não consegui consultar os repositórios."));
            return;
        }

        process->write(header);
        header.fill('x');
        process->closeWriteChannel();
    }

    void showMessage(const QString &message)
    {
        clearLoading();
        auto *label = new QLabel(message);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color: #8FA3B3; padding: 8px 10px;"));
        listLayout_->insertWidget(0, label);
        placeNearAnchor();
    }

    void showRepos(const QList<Repo> &repos)
    {
        clearLoading();

        if (repos.isEmpty()) {
            showMessage(QStringLiteral("Nenhum repositório nesta conta."));
            return;
        }

        for (const Repo &repo : repos) {
            auto *button = new QPushButton;
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumHeight(52);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            auto *row = new QVBoxLayout(button);
            row->setContentsMargins(10, 7, 10, 7);
            row->setSpacing(1);

            auto *name = new QLabel(repo.name);
            name->setObjectName(QStringLiteral("repoName"));
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            QFont nameFont = name->font();
            nameFont.setPointSize(13);
            nameFont.setWeight(QFont::DemiBold);
            name->setFont(nameFont);
            name->setStyleSheet(QStringLiteral("color: #E7F2F6; background: transparent;"));

            auto *detail = new QLabel(repo.detail);
            detail->setObjectName(QStringLiteral("repoDetail"));
            detail->setAttribute(Qt::WA_TransparentForMouseEvents);
            QFont detailFont = detail->font();
            detailFont.setPointSize(11);
            detail->setFont(detailFont);
            detail->setStyleSheet(QStringLiteral("color: #8FA3B3; background: transparent;"));
            row->addWidget(name);
            row->addWidget(detail);

            const QString url = repo.url;
            connect(button, &QPushButton::clicked, this, [this, url]() {
                if (!url.isEmpty()) {
                    QDesktopServices::openUrl(QUrl(url));
                }

                close();
            });
            listLayout_->insertWidget(listLayout_->count() - 1, button);
        }

        placeNearAnchor();
    }

    void placeNearAnchor()
    {
        const bool wasVisible = isVisible();

        if (wasVisible) {
            hide();
        }

        adjustSize();
        const QPoint bottomRight = anchor_->mapToGlobal(QPoint(anchor_->width(), anchor_->height()));
        QRect screen(0, 0, 1280, 800);

        if (anchor_->screen() != nullptr) {
            screen = anchor_->screen()->geometry();
        }

        int x = bottomRight.x() - width();
        int y = bottomRight.y() + 6;
        x = qBound(screen.left() + 8, x, screen.right() - width() - 8);

        if (y + height() > screen.bottom() - 8) {
            y = bottomRight.y() - height() - kStatusGap;
        }

        move(x, y);
        composePlate();

        if (wasVisible) {
            show();
        }
    }

    void composePlate()
    {
        if (width() < 8 || height() < 8) {
            return;
        }

        QPixmap behind;
        QScreen *screen = this->screen();

        if (screen == nullptr && anchor_ != nullptr) {
            screen = anchor_->screen();
        }

        if (screen != nullptr) {
            behind = screen->grabWindow(0, x(), y(), width(), height());
        }

        constexpr int scale = 3;
        QImage sharp(width() * scale, height() * scale, QImage::Format_ARGB32_Premultiplied);
        sharp.fill(Qt::transparent);

        QPainter painter(&sharp);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(scale, scale);

        if (!behind.isNull()) {
            painter.drawPixmap(rect(), behind, behind.rect());
        } else {
            painter.fillRect(rect(), QColor(18, 24, 32, 230));
        }

        const QRectF box(0.5, 0.5, width() - 1.0, height() - 1.0);
        QPainterPath shape;
        shape.addRoundedRect(box, kRadius, kRadius);
        painter.fillPath(shape, QColor(18, 24, 32, 217));
        painter.setPen(QPen(QColor(255, 255, 255, 38), 1.0));
        painter.drawPath(shape);
        painter.end();

        plate_ = QPixmap::fromImage(sharp.scaled(
            size(),
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation));
        update();
    }

    void clearLoading()
    {
        if (loading_ == nullptr) {
            return;
        }

        loading_->deleteLater();
        loading_ = nullptr;
    }

    GitAccount account_;
    QWidget *anchor_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *list_ = nullptr;
    QVBoxLayout *listLayout_ = nullptr;
    QLabel *loading_ = nullptr;
    QPixmap plate_;
    static constexpr int kStatusGap = 6;
    static constexpr int kRadius = 14;
};

} // namespace

StatusBar::StatusBar(QWidget *parent)
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
    setFixedHeight(kBarHeight);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 0, 14, 0);
    layout->setSpacing(8);

    const QString buttonStyle = QStringLiteral(
        "QPushButton { border: none; background: transparent; border-radius: 8px; }"
        "QPushButton:hover { background: rgba(0,229,255,0.16); }");

    clock_ = new QLabel(this);
    clock_->setTextFormat(Qt::RichText);
    QFont clockFont = clock_->font();
    clockFont.setPointSize(12);
    clockFont.setWeight(QFont::Medium);
    clock_->setFont(clockFont);
    layout->addWidget(clock_);
    layout->addStretch();

    const auto trayLabel = [this, layout](QLabel *&slot, const QString &tip) {
        slot = new QLabel(this);
        slot->setToolTip(tip);
        layout->addWidget(slot);
    };
    trayLabel(tempLabel_, QStringLiteral("Temperatura média e máxima"));
    trayLabel(mediaLabel_, QStringLiteral("Mídia em reprodução"));
    trayLabel(layoutLabel_, QStringLiteral("Layout do teclado"));
    trayLabel(netLabel_, QStringLiteral("Taxa de transferência da rede"));
    trayLabel(batteryLabel_, QStringLiteral("Bateria"));
    mediaLabel_->setMaximumWidth(180);

    sourceButton_ = new QPushButton(this);
    sourceButton_->setFlat(true);
    sourceButton_->setCursor(Qt::PointingHandCursor);
    sourceButton_->setFixedSize(28, 22);
    sourceButton_->setIconSize(QSize(18, 18));
    sourceButton_->setIcon(sourceIcon());
    sourceButton_->setToolTip(QStringLiteral("Editar o source do desktop"));
    sourceButton_->setStyleSheet(buttonStyle);
    layout->addWidget(sourceButton_);

    gitButton_ = new QPushButton(this);
    gitButton_->setFlat(true);
    gitButton_->setCursor(Qt::PointingHandCursor);
    gitButton_->setFixedSize(28, 22);
    gitButton_->setIconSize(QSize(18, 18));
    gitButton_->setStyleSheet(buttonStyle);
    layout->addWidget(gitButton_);

    networkButton_ = new QPushButton(this);
    networkButton_->setFlat(true);
    networkButton_->setCursor(Qt::PointingHandCursor);
    networkButton_->setFixedSize(28, 22);
    networkButton_->setIconSize(QSize(18, 18));
    networkButton_->setIcon(networkIcon());
    networkButton_->setToolTip(QStringLiteral("Rede, VPN e Bluetooth"));
    networkButton_->setStyleSheet(buttonStyle);
    layout->addWidget(networkButton_);

    powerButton_ = new QPushButton(this);
    powerButton_->setFlat(true);
    powerButton_->setCursor(Qt::PointingHandCursor);
    powerButton_->setFixedSize(28, 22);
    powerButton_->setIconSize(QSize(18, 18));
    powerButton_->setIcon(powerIcon());
    powerButton_->setToolTip(QStringLiteral("Encerrar sessão ou desligar"));
    powerButton_->setStyleSheet(buttonStyle);
    layout->addWidget(powerButton_);

    refreshClock();
    refreshTray();
    refreshGitButton();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this]() {
        refreshClock();
        refreshTray();
    });
    clockTimer->start(1000);

    auto *raiseTimer = new QTimer(this);
    connect(raiseTimer, &QTimer::timeout, this, [this]() {
        if (popupIsBlockingRaise()) {
            return;
        }

        raise();
    });
    raiseTimer->start(400);

    connect(gitButton_, &QPushButton::clicked, this, [this]() {
        openGitLogin();
    });
    connect(sourceButton_, &QPushButton::clicked, this, [this]() {
        openDesktopSource();
    });
    connect(networkButton_, &QPushButton::clicked, this, [this]() {
        openNetworkMenu();
    });
    connect(powerButton_, &QPushButton::clicked, this, [this]() {
        openPowerMenu();
    });

    auto *logoutShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    logoutShortcut->setContext(Qt::ApplicationShortcut);
    connect(logoutShortcut, &QShortcut::activated, this, []() {
        if (!confirmPower(
                QStringLiteral("Encerrar sessão"),
                QStringLiteral("Sair desta sessão agora?"))) {
            return;
        }

        endLoginSession();
    });

    EdenGlass::watch(this, [this]() {
        refreshClock();
        refreshTray();
        refreshGlassPlate();
        sourceButton_->setIcon(sourceIcon());
        gitButton_->setIcon(gitIcon(gitActive_));
        networkButton_->setIcon(networkIcon());
        powerButton_->setIcon(powerIcon());
        update();
    });
}

void StatusBar::place(const QRect &screen)
{
    screen_ = screen;
    const int width = qMax(360, screen.width() - kTopMargin * 2);
    setGeometry(screen.x() + kTopMargin, screen.y() + kTopMargin, width, kBarHeight);
    refreshGlassPlate();
    updateMask();
}

void StatusBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(0.5, 0.5, width() - 1.0, height() - 1.0);
    QPainterPath shape;
    shape.addRoundedRect(box, kRadius, kRadius);

    if (!glassPlate_.isNull()) {
        painter.setClipPath(shape);
        painter.drawPixmap(rect(), glassPlate_);
        painter.setClipping(false);
    } else {
        painter.fillPath(shape, EdenGlass::current().panelColor());
    }

    painter.setPen(QPen(EdenGlass::current().borderColor(), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
}

void StatusBar::updateMask()
{
    if (width() <= 0 || height() <= 0) {
        clearMask();
        return;
    }

    QBitmap bitmap(size());
    bitmap.fill(Qt::color0);
    QPainter painter(&bitmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::color1);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kRadius, kRadius);
    painter.end();
    setMask(bitmap);
}

void StatusBar::refreshGlassPlate()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const EdenGlass::Appearance look = EdenGlass::current();
    QImage plate(size(), QImage::Format_ARGB32_Premultiplied);
    const QPixmap source(EdenWallpaper::currentPath());

    if (source.isNull() || screen_.isEmpty()) {
        plate.fill(look.dark ? QColor(30, 30, 46) : QColor(255, 255, 255));
    } else {
        const QPixmap covered = source.scaled(
            screen_.size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const int originX = qMax(0, (covered.width() - screen_.width()) / 2);
        const int originY = qMax(0, (covered.height() - screen_.height()) / 2);
        const QPoint globalTopLeft = mapToGlobal(QPoint(0, 0));
        const QRect sample(
            originX + globalTopLeft.x() - screen_.x(),
            originY + globalTopLeft.y() - screen_.y(),
            width(),
            height());
        const QImage piece = covered.copy(sample).toImage();

        if (piece.isNull()) {
            plate.fill(look.dark ? QColor(30, 30, 46) : QColor(255, 255, 255));
        } else {
            const int divisor = look.blurDivisor();
            const QImage small = piece.scaled(
                qMax(1, piece.width() / divisor),
                qMax(1, piece.height() / divisor),
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
            plate = small.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    QPainter frost(&plate);
    frost.fillRect(plate.rect(), look.frostColor());
    QLinearGradient sheen(0, 0, 0, plate.height());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 28 + look.liquid / 5));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    frost.fillRect(plate.rect(), sheen);
    frost.end();

    plate = plate.convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < plate.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(plate.scanLine(y));

        for (int x = 0; x < plate.width(); ++x) {
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
        }
    }

    glassPlate_ = QPixmap::fromImage(plate);
    update();
}

void StatusBar::showEvent(QShowEvent *)
{
    QTimer::singleShot(0, this, [this]() {
        refreshGlassPlate();
        publishDockStrut();
        raise();
    });
}

void StatusBar::refreshClock()
{
    const QLocale locale(QLocale::Portuguese, QLocale::Brazil);
    const QDateTime now = QDateTime::currentDateTime();
    const EdenGlass::Appearance look = EdenGlass::current();
    clock_->setText(QStringLiteral(
        "<span style='color:%1; font-size:15px; font-weight:600;'>%2</span>"
        "<span style='color:%3; font-size:12px;'>&nbsp;&nbsp;%4</span>")
                        .arg(
                            look.textColor().name(),
                            now.toString(QStringLiteral("HH:mm")),
                            look.mutedColor().name(),
                            locale.toString(now, QStringLiteral("ddd, d 'de' MMM"))));
}

void StatusBar::refreshTray()
{
    const EdenMetrics::Snapshot metrics = EdenMetrics::current();
    const EdenGlass::Appearance look = EdenGlass::current();
    const QString style = QStringLiteral("color: %1; background: transparent;").arg(look.textColor().name());

    const auto apply = [&style](QLabel *label) {
        if (label != nullptr) {
            label->setStyleSheet(style);
        }
    };
    apply(tempLabel_);
    apply(mediaLabel_);
    apply(layoutLabel_);
    apply(netLabel_);
    apply(batteryLabel_);

    tempLabel_->setText(
        metrics.tempOk
            ? QStringLiteral("%1° / %2°").arg(metrics.tempAvgC).arg(metrics.tempMaxC)
            : QStringLiteral("—"));
    layoutLabel_->setText(metrics.keyboard.isEmpty() ? QStringLiteral("—") : metrics.keyboard);
    netLabel_->setText(EdenMetrics::formatRate(metrics.netBytesPerSec));

    if (metrics.mediaTitle.isEmpty()) {
        mediaLabel_->hide();
    } else {
        mediaLabel_->setText(metrics.mediaTitle);
        mediaLabel_->show();
    }

    if (metrics.batteryPercent < 0) {
        batteryLabel_->hide();
    } else {
        batteryLabel_->setText(
            QStringLiteral("%1%%2")
                .arg(metrics.batteryPercent)
                .arg(metrics.batteryCharging ? QStringLiteral(" +") : QString()));
        batteryLabel_->show();
    }
}

void StatusBar::refreshGitButton()
{
    const GitAccount account = currentAccount();
    gitActive_ = account.active;
    gitButton_->setIcon(gitIcon(gitActive_));
    gitButton_->setToolTip(
        gitActive_
            ? QStringLiteral("Git · %1").arg(account.username)
            : QStringLiteral("Entrar no Git"));
}

void StatusBar::openDesktopSource()
{
    const QString project = edenProjectDir();

    if (project.isEmpty()) {
        showSourceWarning(
            QStringLiteral("Não encontrei o projeto Eden (CMakeLists.txt / desktop/src)."));
        return;
    }

    if (!openInEditor(project)) {
        showSourceWarning(
            QStringLiteral("Não consegui abrir o código em um editor.\nProjeto: %1")
                .arg(project));
    }
}

void StatusBar::openGitLogin()
{
    const GitAccount account = currentAccount();

    if (account.active) {
        auto *menu = new RepoMenu(account, gitButton_);
        connect(menu, &QObject::destroyed, this, [this]() {
            refreshGitButton();
        });
        menu->open();
        return;
    }

    GitLoginDialog dialog;
    dialog.exec();
    refreshGitButton();
}

void StatusBar::openPowerMenu()
{
    auto *menu = new PowerMenu(powerButton_);
    menu->onChosen = [this](const QString &id) {
        QTimer::singleShot(0, this, [this, id]() {
            if (id == QLatin1String("logout")) {
                if (confirmPower(
                        QStringLiteral("Encerrar sessão"),
                        QStringLiteral("Sair desta sessão agora?"))) {
                    endLoginSession();
                }

                return;
            }

            if (id == QLatin1String("reboot")) {
                if (!confirmPower(
                        QStringLiteral("Reiniciar"),
                        QStringLiteral("Reiniciar o computador agora?"))) {
                    return;
                }

                if (!rebootMachine()) {
                    QMessageBox::warning(
                        nullptr,
                        QStringLiteral("Reiniciar"),
                        QStringLiteral("Não consegui reiniciar o computador."));
                }

                return;
            }

            if (id == QLatin1String("shutdown")) {
                if (!confirmPower(
                        QStringLiteral("Desligar"),
                        QStringLiteral("Desligar o computador agora?"))) {
                    return;
                }

                if (!powerOffMachine()) {
                    QMessageBox::warning(
                        nullptr,
                        QStringLiteral("Desligar"),
                        QStringLiteral("Não consegui desligar o computador."));
                }
            }
        });
    };
    menu->open();
}

void StatusBar::openNetworkMenu()
{
    NetworkMenu::open(networkButton_);
}

void StatusBar::publishDockStrut()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

    if (native == nullptr || native->connection() == nullptr) {
        return;
    }

    xcb_connection_t *connection = native->connection();
    const xcb_window_t window = static_cast<xcb_window_t>(winId());
    const xcb_atom_t typeProperty = internAtom(connection, "_NET_WM_WINDOW_TYPE");
    const xcb_atom_t dockType = internAtom(connection, "_NET_WM_WINDOW_TYPE_DOCK");
    replaceAtom(connection, window, typeProperty, dockType);

    const xcb_atom_t strut = internAtom(connection, "_NET_WM_STRUT");
    const int reservedTop = qMax(0, screen_.y()) + kTopMargin + kBarHeight;
    const uint32_t reserved[] = {0, 0, static_cast<uint32_t>(reservedTop), 0};

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
    const uint32_t startX = static_cast<uint32_t>(qMax(0, screen_.x()));
    const uint32_t endX = static_cast<uint32_t>(qMax(screen_.x(), screen_.right()));
    const uint32_t partialValues[] = {
        0, 0, static_cast<uint32_t>(reservedTop), 0,
        0, 0, 0, 0,
        startX, endX, 0, 0
    };

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

    xcb_unmap_window(connection, window);
    xcb_map_window(connection, window);
    xcb_flush(connection);
}
