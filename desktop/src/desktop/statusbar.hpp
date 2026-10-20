#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

class StatusBar final : public QWidget
{
public:
    explicit StatusBar(QWidget *parent = nullptr);

    void place(const QRect &screen);

    static constexpr int kBarHeight = 28;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refreshClock();
    void refreshGitButton();
    void openGitLogin();
    void openDesktopSource();
    void openPowerMenu();
    void openNetworkMenu();
    void publishDockStrut();

    QRect screen_;
    QLabel *clock_ = nullptr;
    QPushButton *sourceButton_ = nullptr;
    QPushButton *gitButton_ = nullptr;
    QPushButton *networkButton_ = nullptr;
    QPushButton *powerButton_ = nullptr;
    bool gitActive_ = false;
};
