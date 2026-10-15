
#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QContextMenuEvent;
class QFileSystemModel;
class QListView;
class QModelIndex;

class DesktopWindow final : public QWidget
{
public:
    explicit DesktopWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupFileArea();
    void openItem(const QModelIndex &index);
    void showDesktopContextMenu(const QPoint &globalPosition);
    void navigateToDirectory(const QString &path);
    void navigateBack();
    
    QString homeDirectory_;
    QStringList directoryHistory_;

    QListView *fileView_ = nullptr;
    QFileSystemModel *fileSystemModel_ = nullptr;
};
