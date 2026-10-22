#pragma once

#include <QVector>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;

class DesktopWidgets final : public QWidget
{
public:
    explicit DesktopWidgets(QWidget *parent = nullptr);

    void reposition();

protected:
    void paintEvent(QPaintEvent *event) override;

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

    void sampleActivity();
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
};
