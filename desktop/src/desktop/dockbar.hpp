#pragma once

#include "desktop/docksettings.hpp"

#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QIcon;
class QMouseEvent;
class QPaintEvent;
class QPainterPath;
class QProcess;
class QTimer;

class DockBar final : public QWidget
{
public:
    explicit DockBar(QWidget *parent = nullptr);

    void place(const QRect &screen);
    void applySettings(const EdenDock::Settings &settings);
    [[nodiscard]] EdenDock::Settings settings() const;

    static void bind(DockBar *dock);
    static DockBar *instance();

    void notifyOpening(
        const QString &label,
        const QIcon &icon,
        const QStringList &matchClasses = {},
        qint64 pid = 0,
        const QStringList &matchTitles = {},
        const QStringList &launchArgs = {},
        bool terminal = false,
        const QString &iconName = {});
    void noteClosed(const QString &label);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    void showDesktopMenu(int index);
    void showEntryMenu(int index);
    void activateOrLaunch(int index);
    void closeEntry(int index);
    void keepInDock(int index);
    void removeFromDock(int index);
    void startBuild(int index);
    void finishBuild(int exitCode, bool normalExit);

private:
    struct Entry
    {
        QString label;
        QPixmap icon;
        QString iconName;
        QStringList launchArgs;
        bool terminal = false;
        bool separator = false;
        bool desktopMenu = false;
        bool buildsDesktop = false;
        bool launched = false;
        bool loading = false;
        bool ephemeral = false;
        bool closeable = true;
        bool builtin = false;
        int openWindows = 0;
        bool sawWindow = false;
        qint64 loadingStarted = 0;
        QStringList matchClasses;
        QStringList matchTitles;
        QVector<qint64> pids;
        bool (*launch)() = nullptr;
    };

    void buildEntries();
    void relayout();
    void updateMask();
    void refreshGlass();
    void compose();
    void publishDockStrut();
    void beginLoading(int index);
    void tickLoading();
    void syncOpenWindows();
    void syncLabelFont();
    void rememberPid(int index, qint64 pid);
    void savePinnedApps();
    void restoreBuiltinHooks(Entry &entry);
    void showWindowChooser(int index);
    [[nodiscard]] bool canManageDockPresence(int index) const;
    [[nodiscard]] bool entryLooksOpen(int index) const;
    [[nodiscard]] QPainterPath barPath() const;
    [[nodiscard]] bool anyLoading() const;
    [[nodiscard]] bool activateEntry(int index);
    [[nodiscard]] int openWindowCount(int index) const;
    [[nodiscard]] int findEntry(const QString &label) const;
    [[nodiscard]] int separatorIndex() const;
    [[nodiscard]] int iconSize(int index) const;
    [[nodiscard]] int labelHeight() const;
    [[nodiscard]] double bounceProgress(int index) const;
    [[nodiscard]] int bounceOffset(int index) const;
    [[nodiscard]] int entryAt(const QPoint &position) const;

    QRect screen_;
    QVector<Entry> entries_;
    QVector<QRect> iconRects_;
    QRect barRect_;
    QRect labelRect_;
    QRect perfRect_;
    QPixmap glass_;
    QPixmap backdrop_;
    QPixmap composed_;
    QProcess *buildProcess_ = nullptr;
    QTimer *loadingTimer_ = nullptr;
    QTimer *syncTimer_ = nullptr;
    int buildIndex_ = -1;
    int hover_ = -1;
    EdenDock::Settings settings_;

    static DockBar *active_;

    static constexpr int kPerfWidth = 92;
    static constexpr int kPadX = 14;
    static constexpr int kPadY = 10;
    static constexpr int kLabelGap = 8;
    static constexpr int kBounceMs = 1200;
    static constexpr int kBouncePeak = 18;
    static constexpr int kMaxDots = 4;
};
