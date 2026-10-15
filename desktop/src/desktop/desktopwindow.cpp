
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
#include "filemanager/filemanagerwindow.hpp"

#include <QDesktopServices>
#include <QUrl>
#include <QModelIndex>
#include <QProcess>
#include <QStringList>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>

DesktopWindow::DesktopWindow(QWidget *parent)
    : QWidget(parent),
      homeDirectory_(
          QStandardPaths::writableLocation(
              QStandardPaths::HomeLocation
          )
      )
{
    setWindowFlags(
        Qt::Window |
        Qt::FramelessWindowHint
    );

    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle("Eden Desktop");

    setupFileArea();
}

void DesktopWindow::setupFileArea()
{
    fileSystemModel_ = new QFileSystemModel(this);

    fileSystemModel_->setFilter(
        QDir::AllEntries |
        QDir::NoDotAndDotDot
    );

    fileSystemModel_->setRootPath(homeDirectory_);

    fileView_ = new QListView(this);

    fileView_->setModel(fileSystemModel_);
    fileView_->setRootIndex(
        fileSystemModel_->index(homeDirectory_)
    );

    fileView_->setViewMode(QListView::IconMode);
    fileView_->setFlow(QListView::TopToBottom);
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

    fileView_->setStyleSheet(R"(
        QListView {
            background: transparent;
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


    fileView_->setGeometry(rect());
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
}


void DesktopWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    const QPixmap wallpaper(
        "assets/wallpapers/default.jpg"
    );

    if (wallpaper.isNull()) {
        painter.fillRect(rect(), QColor("#20252D"));
        return;
    }

    const QSize scaledSize =
        wallpaper.size().scaled(
            size(),
            Qt::KeepAspectRatioByExpanding
        );

    const QPixmap scaledWallpaper =
        wallpaper.scaled(
            scaledSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );

    const QPoint position(
        (width() - scaledWallpaper.width()) / 2,
        (height() - scaledWallpaper.height()) / 2
    );

    painter.drawPixmap(position, scaledWallpaper);
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

    QAction *selectedAction =
        menu.exec(globalPosition);

    if (selectedAction == refreshAction) {
        fileSystemModel_->setRootPath(QString());

        fileSystemModel_->setRootPath(homeDirectory_);

        fileView_->setRootIndex(
            fileSystemModel_->index(homeDirectory_)
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