#include "desktop/desktopwidgets.hpp"
#include "desktop/glassstyle.hpp"
#include "desktop/sysmetrics.hpp"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

QString weatherText(int code)
{
    switch (code) {
    case 0:
        return QStringLiteral("Céu limpo");
    case 1:
    case 2:
        return QStringLiteral("Parcialmente nublado");
    case 3:
        return QStringLiteral("Nublado");
    case 45:
    case 48:
        return QStringLiteral("Neblina");
    case 51:
    case 53:
    case 55:
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82:
        return QStringLiteral("Chuva");
    case 71:
    case 73:
    case 75:
    case 85:
    case 86:
        return QStringLiteral("Neve");
    case 95:
    case 96:
    case 99:
        return QStringLiteral("Trovoada");
    default:
        return QStringLiteral("Tempo instável");
    }
}

void drawCard(QPainter &painter, const QRect &rect, const QString &title)
{
    const EdenGlass::Appearance look = EdenGlass::current();
    QPainterPath path;
    path.addRoundedRect(rect, EdenGlass::kRadius, EdenGlass::kRadius);
    painter.setPen(QPen(look.borderColor(), 1));
    painter.setBrush(look.panelColor());
    painter.drawPath(path);

    painter.save();
    painter.setClipPath(path);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, look.dark ? 18 : 40));
    painter.drawRect(rect.adjusted(0, 0, 0, -rect.height() / 2));
    painter.restore();

    painter.setPen(look.mutedColor());
    QFont font = painter.font();
    font.setPointSize(9);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
    painter.setFont(font);
    painter.drawText(
        rect.adjusted(16, 12, -16, -rect.height() + 32),
        Qt::AlignLeft | Qt::AlignVCenter,
        title.toUpper());
}

void drawClockFace(QPainter &painter, const QRect &rect, const QTime &time)
{
    const EdenGlass::Appearance look = EdenGlass::current();
    painter.save();
    painter.translate(rect.center());
    const int radius = rect.width() / 2 - 2;
    painter.setPen(QPen(look.borderColor(), 2));
    painter.setBrush(QColor(255, 255, 255, look.dark ? 16 : 48));
    painter.drawEllipse(QPoint(0, 0), radius, radius);

    painter.setPen(QPen(look.textColor(), 2, Qt::SolidLine, Qt::RoundCap));

    for (int hour = 0; hour < 12; ++hour) {
        painter.save();
        painter.rotate(hour * 30.0);
        painter.drawLine(0, -(radius - 5), 0, -(radius - (hour % 3 == 0 ? 14 : 10)));
        painter.restore();
    }

    const auto hand = [&](double turns, int length, int width, const QColor &color) {
        painter.save();
        painter.rotate(turns * 360.0);
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(0, 8, 0, -length);
        painter.restore();
    };

    const double hours = (time.hour() % 12) + time.minute() / 60.0;
    const double minutes = time.minute() + time.second() / 60.0;
    hand(hours / 12.0, radius - 28, 3, look.textColor());
    hand(minutes / 60.0, radius - 16, 2, look.textColor());
    hand(time.second() / 60.0, radius - 14, 1, look.accentColor());
    painter.setPen(Qt::NoPen);
    painter.setBrush(look.accentColor());
    painter.drawEllipse(QPoint(0, 0), 3, 3);
    painter.restore();
}

}  // namespace

DesktopWidgets::DesktopWidgets(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    network_ = new QNetworkAccessManager(this);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        sampleActivity();
        update();
    });
    timer->start(1000);
    sampleActivity();
    requestGeo();

    EdenGlass::watch(this, [this]() {
        update();
    });
}

void DesktopWidgets::reposition()
{
    QWidget *host = parentWidget();

    if (host == nullptr || host->width() < 200) {
        return;
    }

    QRect area = host->rect();

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QPoint topLeft = host->mapFromGlobal(screen->geometry().topLeft());
        area = QRect(topLeft, screen->size()).intersected(host->rect());
    }

    if (area.width() < 200 || area.height() < 400) {
        return;
    }

    constexpr int width = 308;
    constexpr int top = 70;
    constexpr int bottomGap = 120;
    const int height = std::clamp(area.height() - top - bottomGap, 420, 680);
    setGeometry(area.right() - width - 16, area.top() + top, width, height);
    show();
    raise();
}

void DesktopWidgets::sampleActivity()
{
    static QHash<QString, qint64> firstSeen;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> alive;
    const QDir proc(QStringLiteral("/proc"));

    for (const QString &entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        entry.toInt(&numeric);

        if (!numeric) {
            continue;
        }

        QFile command(QStringLiteral("/proc/%1/cmdline").arg(entry));

        if (!command.open(QIODevice::ReadOnly)) {
            continue;
        }

        QByteArray raw = command.readAll();

        if (raw.isEmpty()) {
            continue;
        }

        raw.replace('\0', ' ');
        QString name = QFileInfo(QString::fromUtf8(raw).simplified().section(QLatin1Char(' '), 0, 0)).fileName();

        if (name.isEmpty() || name == QLatin1String("eden-desktop") || name.startsWith(QLatin1Char('['))) {
            continue;
        }

        alive.insert(name);

        if (!firstSeen.contains(name)) {
            firstSeen.insert(name, now);
        }
    }

    for (auto it = firstSeen.begin(); it != firstSeen.end();) {
        if (!alive.contains(it.key())) {
            it = firstSeen.erase(it);
        } else {
            ++it;
        }
    }

    QVector<ActivityRow> rows;
    rows.reserve(firstSeen.size());

    for (auto it = firstSeen.cbegin(); it != firstSeen.cend(); ++it) {
        rows.push_back(ActivityRow{it.key(), it.value()});
    }

    std::sort(rows.begin(), rows.end(), [](const ActivityRow &left, const ActivityRow &right) {
        return left.sinceMs < right.sinceMs;
    });

    if (rows.size() > 5) {
        rows.resize(5);
    }

    activity_ = rows;
}

void DesktopWidgets::requestGeo()
{
    QNetworkRequest request(QUrl(QStringLiteral("https://ipwho.is/")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EdenDesktop/1.0"));
    request.setTransferTimeout(8000);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        readGeo(reply);
    });
}

void DesktopWidgets::requestForecast(double latitude, double longitude, const QString &place)
{
    place_ = place;
    const QString url = QStringLiteral(
        "https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2"
        "&current=temperature_2m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&forecast_days=4&timezone=auto")
                            .arg(latitude, 0, 'f', 4)
                            .arg(longitude, 0, 'f', 4);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EdenDesktop/1.0"));
    request.setTransferTimeout(8000);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        readForecast(reply);
    });
}

void DesktopWidgets::readGeo(QNetworkReply *reply)
{
    reply->deleteLater();
    const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();

    if (reply->error() != QNetworkReply::NoError || !json.value(QStringLiteral("success")).toBool(true)) {
        requestForecast(-23.55, -46.63, QStringLiteral("São Paulo"));
        return;
    }

    const double latitude = json.value(QStringLiteral("latitude")).toDouble();
    const double longitude = json.value(QStringLiteral("longitude")).toDouble();
    QString place = json.value(QStringLiteral("city")).toString();

    if (place.isEmpty()) {
        place = QStringLiteral("Agora");
    }

    requestForecast(latitude, longitude, place);
}

void DesktopWidgets::readForecast(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        summary_ = QStringLiteral("Clima indisponível");
        weatherReady_ = false;
        update();
        return;
    }

    const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonObject current = json.value(QStringLiteral("current")).toObject();
    temperatureC_ = qRound(current.value(QStringLiteral("temperature_2m")).toDouble());
    summary_ = weatherText(current.value(QStringLiteral("weather_code")).toInt());
    const QJsonObject daily = json.value(QStringLiteral("daily")).toObject();
    const QJsonArray dates = daily.value(QStringLiteral("time")).toArray();
    const QJsonArray mins = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    const QJsonArray maxs = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QLocale locale(QLocale::Portuguese, QLocale::Brazil);
    days_.clear();

    for (int index = 1; index < dates.size() && index < 4; ++index) {
        ForecastDay day;
        const QDate date = QDate::fromString(dates.at(index).toString(), Qt::ISODate);
        day.label = locale.toString(date, QStringLiteral("ddd"));
        day.minC = qRound(mins.at(index).toDouble());
        day.maxC = qRound(maxs.at(index).toDouble());
        days_.push_back(day);
    }

    weatherReady_ = true;
    update();
}

void DesktopWidgets::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const EdenGlass::Appearance look = EdenGlass::current();
    const QLocale locale(QLocale::Portuguese, QLocale::Brazil);
    const int gap = 12;
    int clockH = 156;
    int weatherH = 112;
    int activityH = 150;
    int calendarH = height() - clockH - weatherH - activityH - gap * 3;

    if (calendarH < 168) {
        const int deficit = 168 - calendarH;
        activityH = std::max(110, activityH - deficit / 2);
        clockH = std::max(128, clockH - deficit / 2);
        calendarH = height() - clockH - weatherH - activityH - gap * 3;
    }

    int y = 0;
    const QRect clockRect(0, y, width(), clockH);
    y += clockH + gap;
    const QRect weatherRect(0, y, width(), weatherH);
    y += weatherH + gap;
    const QRect calendarRect(0, y, width(), std::max(120, calendarH));
    y += calendarRect.height() + gap;
    const QRect activityRect(0, y, width(), std::max(90, height() - y));

    drawCard(painter, clockRect, QStringLiteral("Relógio"));
    const QRect face(clockRect.left() + 16, clockRect.top() + 40, 96, 96);
    drawClockFace(painter, face, QTime::currentTime());
    painter.setPen(look.textColor());
    QFont timeFont = painter.font();
    timeFont.setPointSize(20);
    timeFont.setWeight(QFont::DemiBold);
    timeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    painter.setFont(timeFont);
    const QRect textColumn = clockRect.adjusted(128, 42, -16, -16);
    painter.drawText(textColumn, Qt::AlignLeft | Qt::AlignTop, QTime::currentTime().toString(QStringLiteral("HH:mm")));
    painter.setPen(look.mutedColor());
    QFont body = painter.font();
    body.setPointSize(10);
    body.setWeight(QFont::Normal);
    painter.setFont(body);
    const EdenMetrics::Snapshot metrics = EdenMetrics::current();
    painter.drawText(
        textColumn.adjusted(0, 36, 0, 0),
        Qt::AlignLeft | Qt::AlignTop,
        QStringLiteral("Ativo há\n%1").arg(EdenMetrics::formatDuration(metrics.uptimeSeconds)));

    drawCard(painter, weatherRect, QStringLiteral("Clima"));

    if (!weatherReady_ && summary_.isEmpty()) {
        painter.setPen(look.mutedColor());
        painter.setFont(body);
        painter.drawText(
            weatherRect.adjusted(16, 36, -16, -12),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("Buscando previsão…"));
    } else {
        painter.setPen(look.textColor());
        QFont tempFont = body;
        tempFont.setPointSize(22);
        tempFont.setWeight(QFont::DemiBold);
        painter.setFont(tempFont);
        painter.drawText(
            weatherRect.adjusted(16, 36, -16, -12),
            Qt::AlignLeft | Qt::AlignTop,
            weatherReady_ ? QStringLiteral("%1°").arg(temperatureC_) : QStringLiteral("—"));
        painter.setFont(body);
        painter.setPen(look.mutedColor());
        const QString placeLine = place_.isEmpty() ? summary_ : QStringLiteral("%1 · %2").arg(place_, summary_);
        painter.drawText(weatherRect.adjusted(78, 44, -16, -36), Qt::AlignLeft | Qt::AlignTop, placeLine);

        int forecastX = weatherRect.left() + 16;
        painter.setPen(look.textColor());

        for (const ForecastDay &day : days_) {
            const QRect slot(forecastX, weatherRect.bottom() - 28, 90, 20);
            painter.drawText(
                slot,
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("%1  %2°/%3°").arg(day.label).arg(day.maxC).arg(day.minC));
            forecastX += 94;
        }
    }

    drawCard(painter, calendarRect, locale.toString(QDate::currentDate(), QStringLiteral("MMMM yyyy")));
    const QDate today = QDate::currentDate();
    const QDate first(today.year(), today.month(), 1);
    const int lead = first.dayOfWeek() - 1;
    const QRect grid = calendarRect.adjusted(14, 40, -14, -12);
    const int cellW = grid.width() / 7;
    const int rows = 6;
    const int cellH = std::max(16, grid.height() / (rows + 1));
    painter.setPen(look.mutedColor());
    painter.setFont(body);
    static const char *weekdays[] = {"seg", "ter", "qua", "qui", "sex", "sáb", "dom"};

    for (int column = 0; column < 7; ++column) {
        painter.drawText(
            QRect(grid.left() + column * cellW, grid.top(), cellW, cellH),
            Qt::AlignCenter,
            QString::fromUtf8(weekdays[column]));
    }

    for (int day = 1; day <= first.daysInMonth(); ++day) {
        const int slot = lead + day - 1;
        const int column = slot % 7;
        const int row = slot / 7;
        const QRect cell(grid.left() + column * cellW, grid.top() + (row + 1) * cellH, cellW, cellH);

        if (day == today.day()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(look.accentColor());
            painter.drawEllipse(cell.center(), 11, 11);
            painter.setPen(QColor(QStringLiteral("#121820")));
        } else {
            painter.setPen(look.textColor());
        }

        painter.drawText(cell, Qt::AlignCenter, QString::number(day));
    }

    drawCard(painter, activityRect, QStringLiteral("Atividade"));
    painter.setFont(body);

    if (activity_.isEmpty()) {
        painter.setPen(look.mutedColor());
        painter.drawText(activityRect.adjusted(16, 40, -16, -12), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("Nenhum aplicativo em uso"));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int rowY = activityRect.top() + 40;

    for (const ActivityRow &row : activity_) {
        const QRect line(activityRect.left() + 16, rowY, activityRect.width() - 32, 22);
        painter.setPen(look.textColor());
        painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, painter.fontMetrics().elidedText(row.name, Qt::ElideRight, line.width() - 84));
        painter.setPen(look.mutedColor());
        painter.drawText(
            line,
            Qt::AlignRight | Qt::AlignVCenter,
            EdenMetrics::formatDuration((now - row.sinceMs) / 1000.0));
        rowY += 22;
    }
}
