#pragma once

#include <QPair>
#include <QPixmap>
#include <QStringList>
#include <QVector>
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
    void loadAsync();
    void showPending();
    void populate(const QVector<QString> &fields);
    void placeNearAnchor();
    void composePlate();
    void applyShape();
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
    void runAndRefresh(const QString &program, const QStringList &arguments);
    void runFirstOk(const QVector<QPair<QString, QStringList>> &steps);
    void runStep(const QVector<QPair<QString, QStringList>> &steps, int index);

    QWidget *anchor_ = nullptr;
    QWidget *body_ = nullptr;
    QVBoxLayout *bodyLayout_ = nullptr;
    QPixmap plate_;
    QPixmap backdrop_;
    int probeTicket_ = 0;
    bool populated_ = false;
    bool busy_ = false;

    static constexpr int kWidth = 300;
    static constexpr int kRadius = 14;
};
