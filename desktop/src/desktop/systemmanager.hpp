#pragma once

#include <QAbstractNativeEventFilter>
#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

class SystemManager final : public QWidget, public QAbstractNativeEventFilter
{
public:
    explicit SystemManager();

    static void install();
    static SystemManager *instance();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    void toggle();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    class MetricCard;
    class SidebarButton;

    void grabHotkey();
    void refreshStats();
    void placeOnScreen();
    void setSection(int section);

    QWidget *sidebar_ = nullptr;
    QWidget *content_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QVector<SidebarButton *> sidebarButtons_;
    QVector<QPushButton *> tabButtons_;
    MetricCard *cpuCard_ = nullptr;
    MetricCard *ramChartCard_ = nullptr;
    MetricCard *ramDetailCard_ = nullptr;
    MetricCard *diskCard_ = nullptr;
    MetricCard *netCard_ = nullptr;
    MetricCard *gpuCard_ = nullptr;
    QTimer *timer_ = nullptr;
    int section_ = 0;

    quint64 lastCpuTotal_ = 0;
    quint64 lastCpuIdle_ = 0;
    quint64 lastDiskRead_ = 0;
    quint64 lastDiskWrite_ = 0;
    quint64 lastNetRx_ = 0;
    quint64 lastNetTx_ = 0;
    qint64 lastSampleMs_ = 0;
    uint32_t lastRelease_ = 0;
    bool haveRelease_ = false;

    static SystemManager *active_;

    static constexpr int kWidth = 1100;
    static constexpr int kHeight = 720;
};
