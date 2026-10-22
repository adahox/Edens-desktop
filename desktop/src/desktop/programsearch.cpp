#include "desktop/programsearch.hpp"
#include "desktop/desktoplaunch.hpp"
#include "desktop/dockbar.hpp"

#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cstdlib>

namespace {

QString localized(const QHash<QString, QString> &fields, const QString &key)
{
    const QString language = QLocale().name();
    const QString exact = fields.value(key + QLatin1Char('[') + language + QLatin1Char(']'));

    if (!exact.isEmpty()) {
        return exact;
    }

    const QString shortName = language.section(QLatin1Char('_'), 0, 0);
    const QString partial = fields.value(key + QLatin1Char('[') + shortName + QLatin1Char(']'));

    if (!partial.isEmpty()) {
        return partial;
    }

    return fields.value(key);
}

bool flagIsTrue(const QString &value)
{
    return value == QLatin1String("true") || value == QLatin1String("1");
}

QStringList splitCommand(QString command)
{
    command.remove(QRegularExpression(QStringLiteral("%[A-Za-z]")));
    QStringList arguments;
    QString current;
    bool quoted = false;
    QChar quote;

    for (const QChar character : command) {
        
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

QHash<QString, QString> desktopFields(const QString &path)
{
    QFile file(path);
    QHash<QString, QString> fields;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fields;
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

        fields.insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
    }

    return fields;
}

QIcon programIcon(const QString &name)
{
    if (name.isEmpty()) {
        return {};
    }

    if (name.contains(QLatin1Char('/'))) {
        return QIcon(name);
    }

    return QIcon::fromTheme(name);
}

QStringList applicationDirectories()
{
    QStringList directories = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation);
    const QString home = QDir::homePath();
    const QString extras[] = {
        home + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/snapd/desktop/applications")
    };

    for (const QString &extra : extras) {
        if (QDir(extra).exists() && !directories.contains(extra)) {
            directories.push_back(extra);
        }
    }

    return directories;
}

}  // namespace

ProgramSearch::ProgramSearch()
{
    setWindowFlags(
        Qt::Tool |
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::BypassWindowManagerHint |
        Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFixedSize(kWidth, kHeight);
    setWindowTitle(QStringLiteral("Pesquisar"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    edit_ = new QLineEdit;
    edit_->setPlaceholderText(QStringLiteral("Pesquisar programas"));
    edit_->setClearButtonEnabled(true);
    edit_->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background: rgba(255, 255, 255, 0.06);"
        "  color: #E7F2F6;"
        "  border: 1px solid rgba(255, 255, 255, 0.15);"
        "  border-radius: 12px;"
        "  padding: 10px 12px;"
        "  font-size: 15px;"
        "}"
    ));
    edit_->installEventFilter(this);

    list_ = new QListWidget;
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setIconSize(QSize(28, 28));
    list_->setSpacing(2);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; color: #E7F2F6; }"
        "QListWidget::item { padding: 8px 8px; border-radius: 10px; color: #E7F2F6; }"
        "QListWidget::item:selected { background: rgba(0,229,255,0.18); color: #00E5FF; }"
    ));

    layout->addWidget(edit_);
    layout->addWidget(list_, 1);

    connect(edit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        applyFilter(text);
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        launchCurrent();
    });
}

void ProgramSearch::install()
{
    auto *search = new ProgramSearch;
    qApp->installNativeEventFilter(search);
    qApp->installEventFilter(search);
}

bool ProgramSearch::isToggleSearchMessage(const void *message) const
{
    const auto *event = static_cast<const xcb_generic_event_t *>(message);

    if ((event->response_type & 0x7f) != XCB_CLIENT_MESSAGE) {
        return false;
    }

    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

    if (native == nullptr || native->connection() == nullptr) {
        return false;
    }

    if (toggleSearchAtom_ == 0) {
        const char name[] = "_EDEN_TOGGLE_SEARCH";
        const auto cookie = xcb_intern_atom(
            native->connection(),
            0,
            static_cast<uint16_t>(sizeof(name) - 1),
            name);
        xcb_intern_atom_reply_t *reply =
            xcb_intern_atom_reply(native->connection(), cookie, nullptr);

        if (reply == nullptr) {
            return false;
        }

        toggleSearchAtom_ = reply->atom;
        std::free(reply);
    }

    if (toggleSearchAtom_ == 0) {
        return false;
    }

    const auto *client =
        reinterpret_cast<const xcb_client_message_event_t *>(message);
    return client->type == toggleSearchAtom_;
}

bool ProgramSearch::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || message == nullptr) {
        return false;
    }

    if (!isToggleSearchMessage(message)) {
        return false;
    }

    QTimer::singleShot(0, this, [this]() {
        toggle();
    });
    return true;
}

void ProgramSearch::toggle()
{
    if (isVisible()) {
        hide();
        return;
    }

    openSearch();
}

void ProgramSearch::openSearch()
{
    reloadPrograms();
    edit_->clear();
    applyFilter({});
    placeOnScreen();
    hide();
    composePlate();
    show();
    raise();
    activateWindow();
    edit_->setFocus();
}

void ProgramSearch::reloadPrograms()
{
    programs_.clear();
    QSet<QString> seen;

    for (const QString &directory : applicationDirectories()) {
        QDirIterator iterator(
            directory,
            {QStringLiteral("*.desktop")},
            QDir::Files,
            QDirIterator::Subdirectories);

        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString identity = QFileInfo(path).fileName();

            if (seen.contains(identity)) {
                continue;
            }

            const QHash<QString, QString> fields = desktopFields(path);
            const QString type = fields.value(QStringLiteral("Type"), QStringLiteral("Application"));

            if (type != QLatin1String("Application") ||
                flagIsTrue(fields.value(QStringLiteral("NoDisplay"))) ||
                flagIsTrue(fields.value(QStringLiteral("Hidden")))) {
                continue;
            }

            const QString name = localized(fields, QStringLiteral("Name"));
            const QStringList arguments = splitCommand(fields.value(QStringLiteral("Exec")));

            const QString tryExec = fields.value(QStringLiteral("TryExec"));

            if (name.isEmpty() || arguments.isEmpty() || !executableExists(arguments.first()) ||
                (!tryExec.isEmpty() && !executableExists(tryExec))) {
                continue;
            }

            Program program;
            program.name = name;
            program.detail = localized(fields, QStringLiteral("GenericName"));

            if (program.detail.isEmpty()) {
                program.detail = localized(fields, QStringLiteral("Comment"));
            }

            program.arguments = arguments;
            program.matchClasses = EdenDesktop::matchHints(fields);
            program.terminal = flagIsTrue(fields.value(QStringLiteral("Terminal")));
            program.iconName = fields.value(QStringLiteral("Icon"));
            program.icon = programIcon(program.iconName);
            program.haystack = (
                name + QLatin1Char(' ') +
                program.detail + QLatin1Char(' ') +
                fields.value(QStringLiteral("Keywords")) + QLatin1Char(' ') +
                arguments.first()
            ).toLower();
            programs_.push_back(program);
            seen.insert(identity);
        }
    }
}

void ProgramSearch::applyFilter(const QString &text)
{
    list_->clear();
    const QString query = text.trimmed().toLower();

    struct Ranked
    {
        int rank = 0;
        int index = 0;
    };

    QVector<Ranked> matches;
    matches.reserve(programs_.size());

    for (int index = 0; index < programs_.size(); ++index) {
        const Program &program = programs_.at(index);
        const QString name = program.name.toLower();
        int rank = 0;

        if (!query.isEmpty()) {
            if (name.startsWith(query)) {
                rank = 0;
            } else if (name.contains(query)) {
                rank = 1;
            } else if (program.haystack.contains(query)) {
                rank = 2;
            } else {
                continue;
            }
        }

        matches.push_back({rank, index});
    }

    std::sort(matches.begin(), matches.end(), [this](const Ranked &left, const Ranked &right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }

        return programs_.at(left.index).name.localeAwareCompare(
                   programs_.at(right.index).name) < 0;
    });

    if (matches.isEmpty()) {
        auto *empty = new QListWidgetItem(QStringLiteral("Nenhum programa encontrado"));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
        return;
    }

    for (const Ranked &match : matches) {
        const Program &program = programs_.at(match.index);
        auto *item = new QListWidgetItem(program.icon, program.name);
        item->setData(Qt::UserRole, match.index);
        item->setSizeHint(QSize(0, program.detail.isEmpty() ? 40 : 52));
        item->setToolTip(program.detail);
        list_->addItem(item);
    }

    list_->setCurrentRow(0);
}

void ProgramSearch::launchCurrent()
{
    QListWidgetItem *item = list_->currentItem();

    if (item == nullptr || !item->data(Qt::UserRole).isValid()) {
        return;
    }

    const int index = item->data(Qt::UserRole).toInt();

    if (index < 0 || index >= programs_.size()) {
        return;
    }

    const Program &program = programs_.at(index);

    if (program.arguments.isEmpty()) {
        return;
    }

    qint64 pid = 0;

    if (EdenDesktop::launchCommand(program.arguments, program.terminal, &pid)) {
        if (DockBar *dock = DockBar::instance()) {
            dock->notifyOpening(
                program.name,
                program.icon,
                program.matchClasses,
                pid,
                {},
                program.arguments,
                program.terminal,
                program.iconName);
        }

        hide();
    }
}

void ProgramSearch::moveSelection(int delta)
{
    if (list_->count() == 0) {
        return;
    }

    const int current = list_->currentRow();
    const int next = std::clamp(current + delta, 0, list_->count() - 1);
    list_->setCurrentRow(next);
}

void ProgramSearch::placeOnScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        return;
    }

    const QRect area = screen->availableGeometry();
    const int x = area.left() + (area.width() - kWidth) / 2;
    const int y = area.top() + 72;
    setGeometry(x, y, kWidth, kHeight);
}

void ProgramSearch::composePlate()
{
    QPixmap behind;
    QScreen *screen = QGuiApplication::screenAt(geometry().center());

    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
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
        painter.fillRect(rect(), QColor(18, 24, 32));
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

void ProgramSearch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    if (plate_.isNull()) {
        painter.fillRect(rect(), QColor(18, 24, 32));
        return;
    }

    painter.drawPixmap(0, 0, plate_);
}

void ProgramSearch::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    edit_->setFocus();
}

bool ProgramSearch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<const QKeyEvent *>(event);

        if (key->key() == Qt::Key_Down) {
            moveSelection(1);
            return true;
        }

        if (key->key() == Qt::Key_Up) {
            moveSelection(-1);
            return true;
        }

        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            launchCurrent();
            return true;
        }

        if (key->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }

    if (isVisible() &&
        event->type() == QEvent::MouseButtonPress &&
        watched != edit_ &&
        watched != list_ &&
        watched != list_->viewport()) {
        const auto *mouse = static_cast<const QMouseEvent *>(event);

        if (!frameGeometry().contains(mouse->globalPosition().toPoint())) {
            hide();
        }
    }

    return QWidget::eventFilter(watched, event);
}
