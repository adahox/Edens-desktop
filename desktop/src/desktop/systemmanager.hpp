#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QLineEdit;

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

    enum Section {
        Processes = 0,
        Performance,
        Services,
        Startup,
        Network,
        SectionCount
    };

    void grabHotkey();
    void placeOnScreen();
    void setSection(int section);
    void refreshAll();
    void refreshPerformance();
    void refreshProcesses();
    void refreshServices();
    void refreshStartup();
    void refreshNetwork();
    void endSelectedProcess();
    void controlSelectedService(const QString &action);
    void toggleSelectedStartup(bool enable);
    [[nodiscard]] QWidget *buildPerformancePage();
    [[nodiscard]] QWidget *buildTablePage(
        const QString &title,
        QTableWidget **table,
        const QStringList &headers,
        QWidget **toolbarOut);

    QWidget *sidebar_ = nullptr;
    QWidget *content_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QVector<SidebarButton *> sidebarButtons_;
    QVector<QPushButton *> tabButtons_;

    QWidget *performancePage_ = nullptr;
    MetricCard *cpuCard_ = nullptr;
    MetricCard *ramChartCard_ = nullptr;
    MetricCard *ramDetailCard_ = nullptr;
    MetricCard *diskCard_ = nullptr;
    MetricCard *netCard_ = nullptr;
    MetricCard *gpuCard_ = nullptr;

    QTableWidget *processTable_ = nullptr;
    QTableWidget *serviceTable_ = nullptr;
    QTableWidget *startupTable_ = nullptr;
    QTableWidget *networkTable_ = nullptr;
    QLineEdit *processFilter_ = nullptr;
    QLabel *processSummary_ = nullptr;

    QTimer *timer_ = nullptr;
    int section_ = Processes;

    quint64 lastCpuTotal_ = 0;
    quint64 lastCpuIdle_ = 0;
    quint64 lastDiskRead_ = 0;
    quint64 lastDiskWrite_ = 0;
    quint64 lastNetRx_ = 0;
    quint64 lastNetTx_ = 0;
    qint64 lastSampleMs_ = 0;
    QHash<qint64, quint64> lastProcessCpu_;
    quint64 lastProcessCpuTotal_ = 0;
    uint32_t lastRelease_ = 0;
    bool haveRelease_ = false;

    static SystemManager *active_;

    static constexpr int kWidth = 1180;
    static constexpr int kHeight = 760;
};
