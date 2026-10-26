#include "desktop/desktopwidgets.hpp"
#include "desktop/glassstyle.hpp"
#include "desktop/sysmetrics.hpp"

#include <QCheckBox>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString widgetConfigPath()
{
    const QString directory = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("widgets"));
}

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
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::OpenHandCursor);
    setToolTip(QStringLiteral("Arraste para mover"));
    loadConfig();
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

bool DesktopWidgets::enabled(const QString &id) const
{
    if (id == QLatin1String("clock")) {
        return clockEnabled_;
    }

    if (id == QLatin1String("weather")) {
        return weatherEnabled_;
    }

    if (id == QLatin1String("calendar")) {
        return calendarEnabled_;
    }

    if (id == QLatin1String("activity")) {
        return activityEnabled_;
    }

    return false;
}

void DesktopWidgets::loadConfig()
{
    clockEnabled_ = false;
    weatherEnabled_ = false;
    calendarEnabled_ = false;
    activityEnabled_ = false;

    QFile file(widgetConfigPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    bool sawX = false;
    bool sawY = false;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        const int split = line.indexOf(QLatin1Char('='));

        if (split <= 0) {
            continue;
        }

        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();
        const bool on = value == QLatin1String("1");

        if (key == QLatin1String("clock")) {
            clockEnabled_ = on;
        } else if (key == QLatin1String("weather")) {
            weatherEnabled_ = on;
        } else if (key == QLatin1String("calendar")) {
            calendarEnabled_ = on;
        } else if (key == QLatin1String("activity")) {
            activityEnabled_ = on;
        } else if (key == QLatin1String("x")) {
            bool ok = false;
            const int parsed = value.toInt(&ok);

            if (ok) {
                posX_ = parsed;
                sawX = true;
            }
        } else if (key == QLatin1String("y")) {
            bool ok = false;
            const int parsed = value.toInt(&ok);

            if (ok) {
                posY_ = parsed;
                sawY = true;
            }
        }
    }

    placed_ = sawX && sawY;
}

void DesktopWidgets::saveConfig()
{
    QSaveFile file(widgetConfigPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QByteArray body =
        "clock=" + QByteArray(clockEnabled_ ? "1" : "0") + "\n"
        "weather=" + QByteArray(weatherEnabled_ ? "1" : "0") + "\n"
        "calendar=" + QByteArray(calendarEnabled_ ? "1" : "0") + "\n"
        "activity=" + QByteArray(activityEnabled_ ? "1" : "0") + "\n";

    if (placed_) {
        body += "x=" + QByteArray::number(posX_) + "\n";
        body += "y=" + QByteArray::number(posY_) + "\n";
    }
    file.write(body);
    file.commit();
}

void DesktopWidgets::configure()
{
    QDialog dialog(window());
    dialog.setWindowTitle(QStringLiteral("Configurar widgets"));
    dialog.setMinimumWidth(340);

    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        QStringLiteral("Marque os widgets que ficam na área de trabalho. Desmarque para remover."),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *clock = new QCheckBox(QStringLiteral("Relógio"), &dialog);
    auto *weather = new QCheckBox(QStringLiteral("Clima"), &dialog);
    auto *calendar = new QCheckBox(QStringLiteral("Calendário"), &dialog);
    auto *activity = new QCheckBox(QStringLiteral("Atividade"), &dialog);
    clock->setChecked(clockEnabled_);
    weather->setChecked(weatherEnabled_);
    calendar->setChecked(calendarEnabled_);
    activity->setChecked(activityEnabled_);
    layout->addWidget(clock);
    layout->addWidget(weather);
    layout->addWidget(calendar);
    layout->addWidget(activity);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    clockEnabled_ = clock->isChecked();
    weatherEnabled_ = weather->isChecked();
    calendarEnabled_ = calendar->isChecked();
    activityEnabled_ = activity->isChecked();
    saveConfig();
    reposition();
    update();
}

void DesktopWidgets::reposition()
{
    const bool any = clockEnabled_ || weatherEnabled_ || calendarEnabled_ || activityEnabled_;

    if (!any) {
        clearMask();
        hide();
        return;
    }

    const QRect area = workArea();

    if (area.width() < 200 || area.height() < 400) {
        return;
    }

    constexpr int width = 308;
    constexpr int top = 70;
    constexpr int gap = 12;
    int height = 0;
    int count = 0;

    if (clockEnabled_) {
        height += 156;
        ++count;
    }

    if (weatherEnabled_) {
        height += 112;
        ++count;
    }

    if (calendarEnabled_) {
        height += 220;
        ++count;
    }

    if (activityEnabled_) {
        height += 150;
        ++count;
    }

    height += gap * std::max(0, count - 1);

    int x = area.right() - width - 16;
    int y = area.top() + top;

    if (placed_) {
        x = posX_;
        y = posY_;
    }

    const int maxX = qMax(area.left(), area.left() + area.width() - width);
    const int maxY = qMax(area.top(), area.top() + area.height() - height);
    x = qBound(area.left(), x, maxX);
    y = qBound(area.top(), y, maxY);
    setGeometry(x, y, width, height);
    updateShape();
    show();

    if (!dragging_) {
        raise();
    }
}

QRect DesktopWidgets::workArea() const
{
    QWidget *host = parentWidget();

    if (host == nullptr || host->width() < 200) {
        return {};
    }

    QRect area = host->rect();

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QPoint topLeft = host->mapFromGlobal(screen->geometry().topLeft());
        area = QRect(topLeft, screen->size()).intersected(host->rect());
    }

    return area;
}

DesktopWidgets::CardRects DesktopWidgets::cardRects() const
{
    constexpr int gap = 12;
    int y = 0;
    const auto take = [&](bool on, int cardHeight) {
        if (!on) {
            return QRect();
        }

        const QRect card(0, y, width(), cardHeight);
        y += cardHeight + gap;
        return card;
    };

    CardRects cards;
    cards.clock = take(clockEnabled_, 156);
    cards.weather = take(weatherEnabled_, 112);
    cards.calendar = take(calendarEnabled_, 220);
    cards.activity = take(activityEnabled_, 150);
    return cards;
}

void DesktopWidgets::updateShape()
{
    const CardRects cards = cardRects();
    QPainterPath shape;
    const auto add = [&shape](const QRect &card) {
        if (!card.isEmpty()) {
            shape.addRoundedRect(QRectF(card), EdenGlass::kRadius, EdenGlass::kRadius);
        }
    };
    add(cards.clock);
    add(cards.weather);
    add(cards.calendar);
    add(cards.activity);

    if (shape.isEmpty()) {
        clearMask();
        return;
    }

    setMask(QRegion(shape.toFillPolygon().toPolygon()));
}

void DesktopWidgets::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    dragging_ = true;
    dragOffset_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
    raise();
    event->accept();
}

void DesktopWidgets::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_ || (event->buttons() & Qt::LeftButton) == 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint desired = mapToParent(event->pos() - dragOffset_);
    const QRect area = workArea();
    int x = desired.x();
    int y = desired.y();

    if (!area.isEmpty()) {
        const int maxX = qMax(area.left(), area.left() + area.width() - width());
        const int maxY = qMax(area.top(), area.top() + area.height() - height());
        x = qBound(area.left(), x, maxX);
        y = qBound(area.top(), y, maxY);
    }

    move(x, y);
    posX_ = x;
    posY_ = y;
    placed_ = true;
    event->accept();
}

void DesktopWidgets::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;
    setCursor(Qt::OpenHandCursor);

    if (placed_) {
        saveConfig();
    }

    event->accept();
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
    const EdenGlass::Appearance look = EdenGlass::current();
    const QLocale locale(QLocale::Portuguese, QLocale::Brazil);
    const CardRects cards = cardRects();
    const QRect clockRect = cards.clock;
    const QRect weatherRect = cards.weather;
    const QRect calendarRect = cards.calendar;
    const QRect activityRect = cards.activity;

    QFont body = painter.font();
    body.setPointSize(10);
    body.setWeight(QFont::Normal);

    if (clockEnabled_) {
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
    painter.setFont(body);
    const EdenMetrics::Snapshot metrics = EdenMetrics::current();
    painter.drawText(
        textColumn.adjusted(0, 36, 0, 0),
        Qt::AlignLeft | Qt::AlignTop,
        QStringLiteral("Ativo há\n%1").arg(EdenMetrics::formatDuration(metrics.uptimeSeconds)));
    }

    if (weatherEnabled_) {
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
    }

    if (calendarEnabled_) {
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
    }

    if (!activityEnabled_) {
        return;
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
