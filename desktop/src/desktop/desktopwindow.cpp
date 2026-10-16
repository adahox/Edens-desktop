
#include "desktopwindow.hpp"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QFileSystemModel>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QResizeEvent>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QCloseEvent>
#include <QApplication>
#include <QStyle>
#include "filemanager/filemanagerwindow.hpp"

namespace {

class DesktopIconProvider final : public QFileIconProvider
{
public:
    QIcon icon(const QFileInfo &info) const override
    {
        const QStyle *style = QApplication::style();

        if (info.isDir()) {
            return style->standardIcon(QStyle::SP_DirIcon);
        }

        return style->standardIcon(QStyle::SP_FileIcon);
    }
};

}  // namespace

#include <QDesktopServices>
#include <QUrl>
#include <QModelIndex>
#include <QProcess>
#include <QStringList>
#include <QDesktopServices>
#include <QUrl>
#include <QBrush>
#include <QCoreApplication>
#include <QDir>
#include <QPalette>

DesktopWindow::DesktopWindow(QWidget *parent)
    : QWidget(parent),
      desktopDirectory_(
          QStandardPaths::writableLocation(
              QStandardPaths::DesktopLocation
          )
      )
{
    if (desktopDirectory_.isEmpty()) {
        desktopDirectory_ = QDir::homePath();
    }

    QDir().mkpath(desktopDirectory_);

    setWindowFlags(
        Qt::Window |
        Qt::FramelessWindowHint |
        Qt::BypassWindowManagerHint
    );

    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle("Eden Desktop");

    setupFileArea();
}

DesktopWindow::~DesktopWindow()
{
    if (fileSystemModel_ != nullptr) {
        fileSystemModel_->setIconProvider(nullptr);
    }

    delete iconProvider_;
    iconProvider_ = nullptr;
}

void DesktopWindow::setupFileArea()
{
    fileSystemModel_ = new QFileSystemModel(this);

    fileSystemModel_->setFilter(
        QDir::AllDirs |
        QDir::Files |
        QDir::NoDotAndDotDot |
        QDir::Readable
    );

    iconProvider_ = new DesktopIconProvider();
    fileSystemModel_->setIconProvider(iconProvider_);
    fileSystemModel_->setRootPath(desktopDirectory_);

    fileView_ = new QListView(this);

    fileView_->setModel(fileSystemModel_);
    fileView_->setViewMode(QListView::IconMode);
    fileView_->setFlow(QListView::LeftToRight);
    fileView_->setMovement(QListView::Snap);
    fileView_->setResizeMode(QListView::Adjust);
    fileView_->setWrapping(true);

    fileView_->setIconSize(QSize(48, 48));
    fileView_->setGridSize(QSize(104, 92));
    fileView_->setSpacing(8);

    fileView_->setWordWrap(true);
    fileView_->setTextElideMode(Qt::ElideMiddle);
    fileView_->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    fileView_->setContextMenuPolicy(
        Qt::CustomContextMenu
    );

    fileView_->setAutoFillBackground(false);
    fileView_->viewport()->setAutoFillBackground(false);
    fileView_->setFrameShape(QFrame::NoFrame);
    fileView_->setStyleSheet(R"(
        QListView {
            border: none;
            color: #FFFFFF;
            outline: none;
            padding: 12px;
        }

        QListView::item {
            background: transparent;
            border-radius: 6px;
            padding: 4px;
        }

        QListView::item:selected {
            background: rgba(100, 150, 220, 100);
        }

        QListView::item:hover {
            background: rgba(255, 255, 255, 25);
        }
    )");

    connect(
        fileView_,
        &QListView::customContextMenuRequested,
        this,
        [this](const QPoint &position) {
            showDesktopContextMenu(
                fileView_->viewport()->mapToGlobal(position)
            );
        }
    );


    connect(
        fileView_,
        &QListView::doubleClicked,
        this,
        [this](const QModelIndex &index) {
            openItem(index);
        }
    );

    connect(
        fileSystemModel_,
        &QFileSystemModel::directoryLoaded,
        this,
        [this](const QString &path) {
            if (QDir(path) != QDir(desktopDirectory_)) {
                return;
            }

            const QModelIndex root =
                fileSystemModel_->index(desktopDirectory_);

            if (root.isValid()) {
                fileView_->setRootIndex(root);
            }
        }
    );

    const QModelIndex root = fileSystemModel_->index(desktopDirectory_);

    if (root.isValid()) {
        fileView_->setRootIndex(root);
    }

    fileView_->setGeometry(rect());
    applyWallpaper();
}

QString DesktopWindow::wallpaperPath() const
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath("../assets/wallpapers/default.jpg"),
        binaryDirectory.filePath("assets/wallpapers/default.jpg"),
        QStringLiteral("assets/wallpapers/default.jpg")
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }

    return QString();
}

void DesktopWindow::applyWallpaper()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QPixmap source(wallpaperPath());

    if (source.isNull()) {
        wallpaper_ = QPixmap(size());
        wallpaper_.fill(QColor("#20252D"));
    } else {
        const QPixmap scaled = source.scaled(
            size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const int x = qMax(0, (scaled.width() - width()) / 2);
        const int y = qMax(0, (scaled.height() - height()) / 2);
        wallpaper_ = scaled.copy(x, y, width(), height());
    }

    if (fileView_ != nullptr && fileView_->viewport() != nullptr) {
        QPalette palette = fileView_->viewport()->palette();
        const QBrush brush(wallpaper_);
        palette.setBrush(QPalette::Base, brush);
        palette.setBrush(QPalette::Window, brush);
        palette.setColor(QPalette::Text, QColor("#FFFFFF"));
        fileView_->viewport()->setPalette(palette);
        fileView_->viewport()->setAutoFillBackground(true);
    }

    update();
}

void DesktopWindow::openItem(const QModelIndex &index)
{
    const QString path = fileSystemModel_->filePath(index);
    const QFileInfo info(path);

    if (info.isDir()) {
        const QString executable = QDir(
            QCoreApplication::applicationDirPath()
        ).filePath("edens");

        const bool started = QProcess::startDetached(
            executable,
            QStringList{path}
        );

        if (!started) {
            qWarning() << "Não foi possível abrir o Eden's Manager:"
                       << executable
                       << path;
        }

        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}



void DesktopWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fileView_->setGeometry(rect());
    applyWallpaper();
}


void DesktopWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    if (wallpaper_.isNull()) {
        painter.fillRect(rect(), QColor("#20252D"));
        return;
    }

    painter.drawPixmap(0, 0, wallpaper_);
}


void DesktopWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
}

void DesktopWindow::contextMenuEvent(QContextMenuEvent *event)
{
    showDesktopContextMenu(event->globalPos());
}

void DesktopWindow::showDesktopContextMenu(
    const QPoint &globalPosition
)
{
    QMenu menu(this);

    QAction *refreshAction =
        menu.addAction("Atualizar");

    QAction *terminalAction =
        menu.addAction("Abrir terminal");

    menu.addSeparator();

    QAction *settingsAction =
        menu.addAction("Configurações do Eden");

    menu.addSeparator();

    QAction *quitAction =
        menu.addAction("Encerrar sessão");

    QAction *selectedAction =
        menu.exec(globalPosition);

    if (selectedAction == refreshAction) {
        fileSystemModel_->setRootPath(QString());

        fileSystemModel_->setRootPath(desktopDirectory_);

        fileView_->setRootIndex(
            fileSystemModel_->index(desktopDirectory_)
        );

        return;
    }

    if (selectedAction == terminalAction) {
        const QStringList terminals = {
            "x-terminal-emulator",
            "gnome-terminal",
            "mate-terminal",
            "xfce4-terminal",
            "xterm"
        };

        for (const QString &terminal : terminals) {
            const QString executable =
                QStandardPaths::findExecutable(terminal);

            if (!executable.isEmpty() &&
                QProcess::startDetached(executable)) {
                return;
            }
        }

        return;
    }

    if (selectedAction == settingsAction) {
        // Implementaremos as configurações posteriormente.
        return;
    }

    if (selectedAction == quitAction) {
        QApplication::quit();
    }
}

void DesktopWindow::navigateToDirectory(const QString &path)
{
    const QModelIndex index =
        fileSystemModel_->index(path);

    if (!index.isValid()) {
        return;
    }

    directoryHistory_.append(
        fileSystemModel_->filePath(fileView_->rootIndex())
    );

    fileView_->setRootIndex(index);
}

void DesktopWindow::navigateBack()
{
    if (directoryHistory_.isEmpty()) {
        return;
    }

    const QString previousPath =
        directoryHistory_.takeLast();

    const QModelIndex index =
        fileSystemModel_->index(previousPath);

    if (index.isValid()) {
        fileView_->setRootIndex(index);
    }
}