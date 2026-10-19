
#pragma once

#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QCloseEvent;
class QContextMenuEvent;
class QShowEvent;
class QFileIconProvider;
class QFileSystemWatcher;
class QListView;
class QStandardItemModel;
class QModelIndex;

class DesktopWindow final : public QWidget
{
public:
    explicit DesktopWindow(QWidget *parent = nullptr);
    ~DesktopWindow() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupFileArea();
    void revealDesktopFiles();
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
    QFileSystemWatcher *directoryWatcher_ = nullptr;

    QListView *fileView_ = nullptr;
    QStandardItemModel *fileModel_ = nullptr;
};
