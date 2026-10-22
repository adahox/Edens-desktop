#pragma once

#include <QPixmap>
#include <QWidget>

class QLabel;
class QPushButton;

class StatusBar final : public QWidget
{
public:
    explicit StatusBar(QWidget *parent = nullptr);

    void place(const QRect &screen);

    static constexpr int kBarHeight = 46;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refreshClock();
    void refreshTray();
    void refreshGlassPlate();
    void updateMask();
    void refreshGitButton();
    void openGitLogin();
    void openDesktopSource();
    void openPowerMenu();
    void openNetworkMenu();
    void publishDockStrut();

    QRect screen_;
    QLabel *clock_ = nullptr;
    QLabel *tempLabel_ = nullptr;
    QLabel *mediaLabel_ = nullptr;
    QLabel *layoutLabel_ = nullptr;
    QLabel *netLabel_ = nullptr;
    QLabel *batteryLabel_ = nullptr;
    QPushButton *sourceButton_ = nullptr;
    QPushButton *gitButton_ = nullptr;
    QPushButton *networkButton_ = nullptr;
    QPushButton *powerButton_ = nullptr;
    QPixmap glassPlate_;
    bool gitActive_ = false;

    static constexpr int kTopMargin = 10;
    static constexpr int kRadius = 18;
};
