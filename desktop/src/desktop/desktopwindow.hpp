
#pragma once

#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QCloseEvent;
class QContextMenuEvent;
class QFileIconProvider;
class QFileSystemModel;
class QListView;
class QModelIndex;

class DesktopWindow final : public QWidget
{
public:
    explicit DesktopWindow(QWidget *parent = nullptr);
    ~DesktopWindow() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupFileArea();
    void applyWallpaper();
    [[nodiscard]] QString wallpaperPath() const;
    void openItem(const QModelIndex &index);
    void showDesktopContextMenu(const QPoint &globalPosition);
    void navigateToDirectory(const QString &path);
    void navigateBack();
    
    QString desktopDirectory_;
    QStringList directoryHistory_;
    QPixmap wallpaper_;
    QFileIconProvider *iconProvider_ = nullptr;

    QListView *fileView_ = nullptr;
    QFileSystemModel *fileSystemModel_ = nullptr;
};
