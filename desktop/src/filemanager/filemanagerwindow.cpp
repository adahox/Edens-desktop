
#include "filemanagerwindow.hpp"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QStandardPaths>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>


FileManagerWindow::FileManagerWindow(
    const QString &initialDirectory,
    QWidget *parent
)
    : QMainWindow(parent)
{
    setupUi();

    const QString directory = initialDirectory.isEmpty()
        ? QStandardPaths::writableLocation(
              QStandardPaths::HomeLocation)
        : initialDirectory;

    navigateTo(directory);
}

void FileManagerWindow::setupUi()
{
    setWindowTitle("Eden File Manager");
    resize(900, 600);

    toolBar_ = addToolBar("Navegação");
    toolBar_->setMovable(false);

    QAction *backAction = toolBar_->addAction("Voltar");
    QAction *forwardAction = toolBar_->addAction("Avançar");
    QAction *homeAction = toolBar_->addAction("Pasta pessoal");

    connect(backAction, &QAction::triggered,
            this, [this] { navigateBack(); });

    connect(forwardAction, &QAction::triggered,
            this, [this] { navigateForward(); });

    connect(homeAction, &QAction::triggered,
            this, [this] { navigateHome(); });

    locationEdit_ = new QLineEdit(this);
    locationEdit_->setPlaceholderText("Caminho da pasta");
    toolBar_->addWidget(locationEdit_);

    connect(locationEdit_, &QLineEdit::returnPressed,
            this, [this] {
                navigateTo(locationEdit_->text());
            });

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    fileSystemModel_ = new QFileSystemModel(this);
    fileSystemModel_->setFilter(
        QDir::AllEntries | QDir::NoDotAndDotDot
    );

    fileSystemModel_->setRootPath(QDir::rootPath());

    fileView_ = new QListView(centralWidget);
    fileView_->setModel(fileSystemModel_);
    fileView_->setViewMode(QListView::IconMode);
    fileView_->setResizeMode(QListView::Adjust);
    fileView_->setMovement(QListView::Static);
    fileView_->setIconSize(QSize(48, 48));
    fileView_->setGridSize(QSize(110, 100));

    layout->addWidget(fileView_);
    setCentralWidget(centralWidget);

    connect(fileView_, &QListView::doubleClicked,
            this, [this](const QModelIndex &index) {
                const QString path =
                    fileSystemModel_->filePath(index);

                const QFileInfo info(path);

                if (info.isDir()) {
                    navigateTo(path);
                }
            });
}

void FileManagerWindow::navigateTo(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        locationEdit_->setText(currentDirectory_);
        return;
    }

    const QString absolutePath = info.absoluteFilePath();

    if (absolutePath == currentDirectory_) {
        locationEdit_->setText(currentDirectory_);
        return;
    }

    if (historyIndex_ + 1 < history_.size()) {
        history_.erase(
            history_.begin() + historyIndex_ + 1,
            history_.end()
        );
    }

    history_.append(absolutePath);
    historyIndex_ = history_.size() - 1;

    currentDirectory_ = absolutePath;

    fileView_->setRootIndex(
        fileSystemModel_->index(currentDirectory_)
    );

    locationEdit_->setText(currentDirectory_);
    setWindowTitle(
        QFileInfo(currentDirectory_).fileName().isEmpty()
            ? currentDirectory_
            : QFileInfo(currentDirectory_).fileName()
              + " — Eden File Manager"
    );

    updateNavigationActions();
}

void FileManagerWindow::navigateBack()
{
    if (historyIndex_ <= 0) {
        return;
    }

    --historyIndex_;
    currentDirectory_ = history_.at(historyIndex_);

    fileView_->setRootIndex(
        fileSystemModel_->index(currentDirectory_)
    );

    locationEdit_->setText(currentDirectory_);
    updateNavigationActions();
}

void FileManagerWindow::navigateForward()
{
    if (historyIndex_ + 1 >= history_.size()) {
        return;
    }

    ++historyIndex_;
    currentDirectory_ = history_.at(historyIndex_);

    fileView_->setRootIndex(
        fileSystemModel_->index(currentDirectory_)
    );

    locationEdit_->setText(currentDirectory_);
    updateNavigationActions();
}

void FileManagerWindow::navigateHome()
{
    navigateTo(
        QStandardPaths::writableLocation(
            QStandardPaths::HomeLocation
        )
    );
}

void FileManagerWindow::updateNavigationActions()
{
    const QList<QAction *> actions = toolBar_->actions();

    if (actions.size() >= 3) {
        actions.at(0)->setEnabled(historyIndex_ > 0);
        actions.at(1)->setEnabled(
            historyIndex_ + 1 < history_.size()
        );
    }
}
