#pragma once

#include <QPixmap>
#include <QWidget>

#include <functional>

class QPushButton;
class QVBoxLayout;

class NetworkMenu final : public QWidget
{
public:
    explicit NetworkMenu(QWidget *anchor);

    static void open(QPushButton *anchor);

    enum class Kind {
        Wifi,
        Bluetooth,
        Ethernet,
        Vpn,
        WifiNetwork
    };

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void placeNearAnchor();
    void composePlate();
    void clearRows();
    void addHeader();
    void addSection(const QString &title);
    void addCard(
        Kind kind,
        const QString &title,
        const QString &detail,
        bool enabled,
        bool available,
        bool active,
        const QColor &toggleOn,
        const std::function<void(bool)> &onToggle);
    void scheduleRefresh();
    void runAndRefresh(const QString &program, const QStringList &arguments);

    QWidget *anchor_ = nullptr;
    QWidget *body_ = nullptr;
    QVBoxLayout *bodyLayout_ = nullptr;
    QPixmap plate_;

    static constexpr int kWidth = 300;
    static constexpr int kRadius = 14;
};
