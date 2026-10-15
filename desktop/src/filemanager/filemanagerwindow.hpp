
#pragma once

#include <QMainWindow>
#include <QString>

class QFileSystemModel;
class QLineEdit;
class QListView;
class QToolBar;
class QModelIndex; 

class FileManagerWindow final : public QMainWindow
{
public:
    explicit FileManagerWindow(
        const QString &initialDirectory = QString(),
        QWidget *parent = nullptr
    );

private:
    void setupUi();
    void navigateTo(const QString &path);
    void navigateBack();
    void navigateForward();
    void navigateHome();
    void updateNavigationActions();
    void openItem(const QModelIndex &index);

    QFileSystemModel *fileSystemModel_ = nullptr;
    QListView *fileView_ = nullptr;
    QLineEdit *locationEdit_ = nullptr;
    QToolBar *toolBar_ = nullptr;

    QString currentDirectory_;
    QStringList history_;
    int historyIndex_ = -1;
};
