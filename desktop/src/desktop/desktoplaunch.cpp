#include "desktop/desktoplaunch.hpp"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <cstring>

namespace {

QString localized(const QHash<QString, QString> &fields, const QString &key)
{
    const QString language = QLocale().name();
    const QString exact =
        fields.value(key + QLatin1Char('[') + language + QLatin1Char(']'));

    if (!exact.isEmpty()) {
        return exact;
    }

    const QString shortName = language.section(QLatin1Char('_'), 0, 0);
    const QString partial =
        fields.value(key + QLatin1Char('[') + shortName + QLatin1Char(']'));

    if (!partial.isEmpty()) {
        return partial;
    }

    return fields.value(key);
}

bool flagIsTrue(const QString &value)
{
    return value == QLatin1String("true") || value == QLatin1String("1");
}

bool executableExists(const QString &command)
{
    if (command.isEmpty()) {
        return false;
    }

    if (command.contains(QLatin1Char('/'))) {
        return QFileInfo::exists(command);
    }

    return !QStandardPaths::findExecutable(command).isEmpty();
}

xcb_connection_t *x11()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return native == nullptr ? nullptr : native->connection();
}

xcb_atom_t intern(xcb_connection_t *connection, const char *name)
{
    const auto cookie = xcb_intern_atom(
        connection,
        0,
        static_cast<uint16_t>(std::strlen(name)),
        name);
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }

    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

xcb_window_t rootWindow(xcb_connection_t *connection)
{
    const xcb_setup_t *setup = xcb_get_setup(connection);

    if (setup == nullptr) {
        return XCB_WINDOW_NONE;
    }

    const xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    return screen == nullptr ? XCB_WINDOW_NONE : screen->root;
}

QString readStringProperty(
    xcb_connection_t *connection,
    xcb_window_t window,
    xcb_atom_t property)
{
    const auto cookie = xcb_get_property(
        connection,
        0,
        window,
        property,
        XCB_GET_PROPERTY_TYPE_ANY,
        0,
        256);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(connection, cookie, nullptr);

    if (reply == nullptr || xcb_get_property_value_length(reply) <= 0) {
        std::free(reply);
        return {};
    }

    const QByteArray bytes(
        static_cast<const char *>(xcb_get_property_value(reply)),
        xcb_get_property_value_length(reply));
    std::free(reply);

    const int nul = bytes.indexOf('\0');
    return QString::fromUtf8(bytes.left(nul < 0 ? bytes.size() : nul));
}

QStringList readClassProperty(xcb_connection_t *connection, xcb_window_t window)
{
    const auto cookie = xcb_get_property(
        connection,
        0,
        window,
        XCB_ATOM_WM_CLASS,
        XCB_ATOM_STRING,
        0,
        256);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(connection, cookie, nullptr);

    if (reply == nullptr || xcb_get_property_value_length(reply) <= 0) {
        std::free(reply);
        return {};
    }

    const char *data = static_cast<const char *>(xcb_get_property_value(reply));
    const int length = xcb_get_property_value_length(reply);
    QStringList parts;
    int start = 0;

    for (int index = 0; index < length; ++index) {
        if (data[index] != '\0') {
            continue;
        }

        if (index > start) {
            parts.push_back(QString::fromUtf8(data + start, index - start));
        }

        start = index + 1;

        if (parts.size() >= 2) {
            break;
        }
    }

    std::free(reply);
    return parts;
}

bool atomListContains(
    xcb_connection_t *connection,
    xcb_window_t window,
    xcb_atom_t property,
    xcb_atom_t needle)
{
    if (property == XCB_ATOM_NONE || needle == XCB_ATOM_NONE) {
        return false;
    }

    const auto cookie = xcb_get_property(
        connection,
        0,
        window,
        property,
        XCB_ATOM_ATOM,
        0,
        64);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return false;
    }

    const auto *atoms = static_cast<const xcb_atom_t *>(xcb_get_property_value(reply));
    const int count =
        xcb_get_property_value_length(reply) / static_cast<int>(sizeof(xcb_atom_t));
    bool found = false;

    for (int index = 0; index < count; ++index) {
        if (atoms[index] == needle) {
            found = true;
            break;
        }
    }

    std::free(reply);
    return found;
}

bool matchesHints(
    const EdenDesktop::ClientWindow &client,
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints)
{
    for (qint64 pid : pids) {
        if (pid > 0 && client.pid == pid) {
            return true;
        }
    }

    for (const QString &hint : classHints) {
        if (hint.isEmpty()) {
            continue;
        }

        if (client.instance.compare(hint, Qt::CaseInsensitive) == 0 ||
            client.className.compare(hint, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    for (const QString &hint : titleHints) {
        if (!hint.isEmpty() &&
            client.title.contains(hint, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

}  // namespace

namespace EdenDesktop {

QHash<QString, QString> fields(const QString &desktopPath)
{
    QFile file(desktopPath);
    QHash<QString, QString> values;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return values;
    }

    bool insideEntry = false;

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('['))) {
            if (insideEntry) {
                break;
            }

            insideEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }

        if (!insideEntry) {
            continue;
        }

        const int separator = line.indexOf(QLatin1Char('='));

        if (separator <= 0) {
            continue;
        }

        values.insert(
            line.left(separator).trimmed(),
            line.mid(separator + 1).trimmed());
    }

    return values;
}

QString localizedName(const QHash<QString, QString> &desktopFields)
{
    return localized(desktopFields, QStringLiteral("Name"));
}

QIcon iconFor(const QHash<QString, QString> &desktopFields)
{
    const QString name = desktopFields.value(QStringLiteral("Icon"));

    if (name.isEmpty()) {
        return {};
    }

    if (name.contains(QLatin1Char('/'))) {
        return QIcon(name);
    }

    return QIcon::fromTheme(name);
}

QStringList matchHints(const QHash<QString, QString> &desktopFields)
{
    QStringList hints;
    const QString startup = desktopFields.value(QStringLiteral("StartupWMClass"));

    if (!startup.isEmpty()) {
        hints.push_back(startup);
    }

    const QStringList arguments =
        splitExec(desktopFields.value(QStringLiteral("Exec")));

    if (!arguments.isEmpty()) {
        const QString base = QFileInfo(arguments.first()).completeBaseName();
        const QString fileName = QFileInfo(arguments.first()).fileName();

        if (!fileName.isEmpty() && !hints.contains(fileName, Qt::CaseInsensitive)) {
            hints.push_back(fileName);
        }

        if (!base.isEmpty() && !hints.contains(base, Qt::CaseInsensitive)) {
            hints.push_back(base);
        }
    }

    return hints;
}

QStringList splitExec(const QString &command)
{
    QString cleaned = command;
    cleaned.remove(QRegularExpression(QStringLiteral("%[A-Za-z]")));
    QStringList arguments;
    QString current;
    bool quoted = false;
    QChar quote;

    for (const QChar character : cleaned) {
        if (quoted) {
            if (character == quote) {
                quoted = false;
            } else {
                current += character;
            }

            continue;
        }

        if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
            quoted = true;
            quote = character;
            continue;
        }

        if (character.isSpace()) {
            if (!current.isEmpty()) {
                arguments.push_back(current);
                current.clear();
            }

            continue;
        }

        current += character;
    }

    if (!current.isEmpty()) {
        arguments.push_back(current);
    }

    return arguments;
}

bool launchCommand(const QStringList &arguments, bool terminal, qint64 *pid)
{
    if (arguments.isEmpty()) {
        return false;
    }

    QStringList argv = arguments;
    const QString executable = argv.takeFirst();

    if (terminal) {
        const QStringList terminals = {
            QStringLiteral("x-terminal-emulator"),
            QStringLiteral("gnome-terminal"),
            QStringLiteral("xfce4-terminal"),
            QStringLiteral("xterm")
        };

        for (const QString &terminalName : terminals) {
            const QString path = QStandardPaths::findExecutable(terminalName);

            if (path.isEmpty()) {
                continue;
            }

            QStringList terminalArguments;

            if (terminalName == QLatin1String("gnome-terminal")) {
                terminalArguments << QStringLiteral("--");
            } else {
                terminalArguments << QStringLiteral("-e");
            }

            terminalArguments << executable;
            terminalArguments.append(argv);

            if (QProcess::startDetached(path, terminalArguments, QString(), pid)) {
                return true;
            }
        }

        return false;
    }

    QString resolved = executable;

    if (!executable.contains(QLatin1Char('/'))) {
        resolved = QStandardPaths::findExecutable(executable);
    }

    if (resolved.isEmpty()) {
        return false;
    }

    return QProcess::startDetached(resolved, argv, QString(), pid);
}

bool launchDesktopFile(const QString &desktopPath, qint64 *pid)
{
    const QHash<QString, QString> desktopFields = fields(desktopPath);
    const QString type =
        desktopFields.value(QStringLiteral("Type"), QStringLiteral("Application"));

    if (type != QLatin1String("Application") ||
        flagIsTrue(desktopFields.value(QStringLiteral("Hidden"))) ||
        flagIsTrue(desktopFields.value(QStringLiteral("NoDisplay")))) {
        return false;
    }

    const QStringList arguments = splitExec(desktopFields.value(QStringLiteral("Exec")));
    const QString tryExec = desktopFields.value(QStringLiteral("TryExec"));

    if (arguments.isEmpty() || !executableExists(arguments.first()) ||
        (!tryExec.isEmpty() && !executableExists(tryExec))) {
        return false;
    }

    return launchCommand(
        arguments,
        flagIsTrue(desktopFields.value(QStringLiteral("Terminal"))),
        pid);
}

QVector<ClientWindow> listClientWindows()
{
    QVector<ClientWindow> clients;
    xcb_connection_t *connection = x11();

    if (connection == nullptr) {
        return clients;
    }

    const xcb_window_t root = rootWindow(connection);
    const xcb_atom_t netClientList = intern(connection, "_NET_CLIENT_LIST");
    const xcb_atom_t netWmPid = intern(connection, "_NET_WM_PID");
    const xcb_atom_t netWmName = intern(connection, "_NET_WM_NAME");
    const xcb_atom_t netWmState = intern(connection, "_NET_WM_STATE");
    const xcb_atom_t netWmStateHidden = intern(connection, "_NET_WM_STATE_HIDDEN");

    if (root == XCB_WINDOW_NONE || netClientList == XCB_ATOM_NONE) {
        return clients;
    }

    const auto cookie = xcb_get_property(
        connection,
        0,
        root,
        netClientList,
        XCB_ATOM_WINDOW,
        0,
        1024);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return clients;
    }

    const auto *windows =
        static_cast<const xcb_window_t *>(xcb_get_property_value(reply));
    const int count =
        xcb_get_property_value_length(reply) / static_cast<int>(sizeof(xcb_window_t));

    clients.reserve(count);

    for (int index = 0; index < count; ++index) {
        ClientWindow client;
        client.window = windows[index];

        const QStringList classes = readClassProperty(connection, client.window);
        client.instance = classes.value(0);
        client.className = classes.value(1);
        client.title = readStringProperty(connection, client.window, netWmName);

        if (client.title.isEmpty()) {
            client.title =
                readStringProperty(connection, client.window, XCB_ATOM_WM_NAME);
        }

        if (netWmPid != XCB_ATOM_NONE) {
            const auto pidCookie = xcb_get_property(
                connection,
                0,
                client.window,
                netWmPid,
                XCB_ATOM_CARDINAL,
                0,
                1);
            xcb_get_property_reply_t *pidReply =
                xcb_get_property_reply(connection, pidCookie, nullptr);

            if (pidReply != nullptr &&
                xcb_get_property_value_length(pidReply) >= 4) {
                client.pid = *static_cast<const uint32_t *>(
                    xcb_get_property_value(pidReply));
            }

            std::free(pidReply);
        }

        client.hidden = atomListContains(
            connection,
            client.window,
            netWmState,
            netWmStateHidden);
        clients.push_back(client);
    }

    std::free(reply);
    return clients;
}

bool activateWindow(quint32 window)
{
    xcb_connection_t *connection = x11();

    if (connection == nullptr || window == 0) {
        return false;
    }

    const xcb_window_t root = rootWindow(connection);
    const xcb_atom_t netActive = intern(connection, "_NET_ACTIVE_WINDOW");
    const xcb_atom_t netWmState = intern(connection, "_NET_WM_STATE");
    const xcb_atom_t netWmStateHidden = intern(connection, "_NET_WM_STATE_HIDDEN");

    if (root == XCB_WINDOW_NONE || netActive == XCB_ATOM_NONE) {
        return false;
    }

    if (netWmState != XCB_ATOM_NONE && netWmStateHidden != XCB_ATOM_NONE) {
        xcb_client_message_event_t unhide = {};
        unhide.response_type = XCB_CLIENT_MESSAGE;
        unhide.format = 32;
        unhide.window = window;
        unhide.type = netWmState;
        unhide.data.data32[0] = 0;  // _NET_WM_STATE_REMOVE
        unhide.data.data32[1] = netWmStateHidden;
        unhide.data.data32[2] = 0;
        unhide.data.data32[3] = 1;
        xcb_send_event(
            connection,
            0,
            root,
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
            reinterpret_cast<const char *>(&unhide));
    }

    xcb_client_message_event_t message = {};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = window;
    message.type = netActive;
    message.data.data32[0] = 2;  // pager
    message.data.data32[1] = XCB_CURRENT_TIME;
    message.data.data32[2] = 0;

    xcb_send_event(
        connection,
        0,
        root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<const char *>(&message));
    xcb_flush(connection);
    return true;
}

bool closeWindow(quint32 window)
{
    xcb_connection_t *connection = x11();

    if (connection == nullptr || window == 0) {
        return false;
    }

    const xcb_window_t root = rootWindow(connection);
    const xcb_atom_t netClose = intern(connection, "_NET_CLOSE_WINDOW");

    if (root == XCB_WINDOW_NONE || netClose == XCB_ATOM_NONE) {
        return false;
    }

    xcb_client_message_event_t message = {};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = window;
    message.type = netClose;
    message.data.data32[0] = XCB_CURRENT_TIME;
    message.data.data32[1] = 2;

    xcb_send_event(
        connection,
        0,
        root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<const char *>(&message));
    xcb_flush(connection);
    return true;
}

bool activateMatching(
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints)
{
    quint32 hiddenWindow = 0;
    quint32 visibleWindow = 0;

    for (const ClientWindow &client : listClientWindows()) {
        if (!matchesHints(client, classHints, pids, titleHints)) {
            continue;
        }

        if (client.hidden) {
            hiddenWindow = client.window;
            break;
        }

        if (visibleWindow == 0) {
            visibleWindow = client.window;
        }
    }

    if (hiddenWindow != 0) {
        return activateWindow(hiddenWindow);
    }

    if (visibleWindow != 0) {
        return activateWindow(visibleWindow);
    }

    return false;
}

bool closeMatching(
    const QStringList &classHints,
    const QVector<qint64> &pids,
    const QStringList &titleHints)
{
    bool closed = false;

    for (const ClientWindow &client : listClientWindows()) {
        if (!matchesHints(client, classHints, pids, titleHints)) {
            continue;
        }

        closed = closeWindow(client.window) || closed;
    }

    return closed;
}

}  // namespace EdenDesktop
