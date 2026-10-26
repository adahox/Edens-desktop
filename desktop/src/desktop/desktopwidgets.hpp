#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;

class DesktopWidgets final : public QWidget
{
public:
    explicit DesktopWidgets(QWidget *parent = nullptr);

    void reposition();
    void configure();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct ForecastDay
    {
        QString label;
        int minC = 0;
        int maxC = 0;
    };

    struct ActivityRow
    {
        QString name;
        qint64 sinceMs = 0;
    };

    struct CardRects
    {
        QRect clock;
        QRect weather;
        QRect calendar;
        QRect activity;
    };

    void sampleActivity();
    void loadConfig();
    void saveConfig();
    [[nodiscard]] QRect workArea() const;
    [[nodiscard]] CardRects cardRects() const;
    void updateShape();
    [[nodiscard]] bool enabled(const QString &id) const;
    void requestGeo();
    void requestForecast(double latitude, double longitude, const QString &place);
    void readGeo(QNetworkReply *reply);
    void readForecast(QNetworkReply *reply);

    QNetworkAccessManager *network_ = nullptr;
    QString place_;
    int temperatureC_ = 0;
    QString summary_;
    bool weatherReady_ = false;
    QVector<ForecastDay> days_;
    QVector<ActivityRow> activity_;
    bool clockEnabled_ = false;
    bool weatherEnabled_ = false;
    bool calendarEnabled_ = false;
    bool activityEnabled_ = false;
    bool placed_ = false;
    bool dragging_ = false;
    QPoint dragOffset_;
    int posX_ = 0;
    int posY_ = 0;
};
