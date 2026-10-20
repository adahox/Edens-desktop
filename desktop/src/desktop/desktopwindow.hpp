
#pragma once

#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

class DesktopRipple;

class QCloseEvent;
class QContextMenuEvent;
class QEvent;
class QKeyEvent;
class QMoveEvent;
class QShowEvent;
class QFileIconProvider;
class QFileSystemWatcher;
class QListView;
class QStandardItem;
class QStandardItemModel;
class QModelIndex;
class QTimer;

class DesktopWindow final : public QWidget
{
public:
    explicit DesktopWindow(QWidget *parent = nullptr);
    ~DesktopWindow() override;

    static void allowSessionExit();
    void refreshWallpaper();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum {
        RolePath = Qt::UserRole,
        RoleIsImage = Qt::UserRole + 1,
        RolePosX = Qt::UserRole + 2,
        RolePosY = Qt::UserRole + 3
    };

    void setupFileArea();
    void revealDesktopFiles();
    void applyIconPositions();
    void applyWallpaper();
    void saveDesktopLayout();
    void scheduleSaveDesktopLayout();
    void scheduleRevealDesktopFiles();
    void renameDesktopItem(QStandardItem *item);
    void beginRenameSelected();
    [[nodiscard]] QHash<QString, QPoint> loadDesktopLayout() const;
    [[nodiscard]] QPoint nextFreeIconPosition(const QHash<QString, QPoint> &used) const;
    [[nodiscard]] QString wallpaperPath() const;
    [[nodiscard]] QString layoutConfigPath() const;
    void openItem(const QModelIndex &index);
    void showDesktopContextMenu(const QPoint &globalPosition);
    void playRipple(const QPoint &localPosition);
    void navigateToDirectory(const QString &path);
    void navigateBack();
    bool pasteClipboardImage();

    QString desktopDirectory_;
    QStringList directoryHistory_;
    QPixmap wallpaper_;
    QFileIconProvider *iconProvider_ = nullptr;
    QFileSystemWatcher *directoryWatcher_ = nullptr;
    QTimer *layoutSaveTimer_ = nullptr;
    QTimer *revealTimer_ = nullptr;
    bool restoringLayout_ = false;

    QListView *fileView_ = nullptr;
    QStandardItemModel *fileModel_ = nullptr;
    DesktopRipple *ripple_ = nullptr;

    static bool sessionExitAllowed_;
};
