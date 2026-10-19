#pragma once

#include <QPixmap>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QProcess;

class DockBar final : public QWidget
{
public:
    explicit DockBar(QWidget *parent = nullptr);

    void place(const QRect &screen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    void showDesktopMenu(int index);
    void startBuild(int index);
    void finishBuild(int exitCode, bool normalExit);

private:
    struct Entry
    {
        QString label;
        QPixmap icon;
        bool separator = false;
        bool desktopMenu = false;
        bool buildsDesktop = false;
        bool launched = false;
        bool (*launch)() = nullptr;
    };

    void buildEntries();
    void relayout();
    void updateMask();
    void refreshGlass();
    void compose();
    [[nodiscard]] int iconSize(int index) const;
    [[nodiscard]] int entryAt(const QPoint &position) const;

    QRect screen_;
    QVector<Entry> entries_;
    QVector<QRect> iconRects_;
    QRect barRect_;
    QRect labelRect_;
    QPixmap glass_;
    QPixmap backdrop_;
    QPixmap composed_;
    QProcess *buildProcess_ = nullptr;
    int buildIndex_ = -1;
    int hover_ = -1;

    static constexpr int kIconRest = 48;
    static constexpr int kIconHover = 64;
    static constexpr int kGap = 8;
    static constexpr int kPadX = 14;
    static constexpr int kPadY = 10;
    static constexpr int kLabelHeight = 28;
    static constexpr int kLabelGap = 8;
    static constexpr int kBottomMargin = 6;
};
