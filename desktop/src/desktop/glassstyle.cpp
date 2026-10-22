#include "desktop/glassstyle.hpp"
#include "desktop/snowtheme.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

struct Watch
{
    QPointer<QObject> owner;
    std::function<void()> fn;
};

EdenGlass::Appearance gAppearance;
bool gLoaded = false;
std::vector<Watch> gWatches;

QString configPath()
{
    const QString directory = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("glass"));
}

void notify()
{
    std::vector<Watch> alive;
    alive.reserve(gWatches.size());

    for (Watch &watch : gWatches) {
        if (watch.owner == nullptr) {
            continue;
        }

        watch.fn();
        alive.push_back(std::move(watch));
    }

    gWatches.swap(alive);
}

QString argb(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

}  // namespace

namespace EdenGlass {

void Appearance::clamp()
{
    blur = std::clamp(blur, 4, 40);
    liquid = std::clamp(liquid, 0, 100);
}

int Appearance::alpha() const
{
    const int base = dark ? 120 : 100;
    return std::clamp(base + liquid, 70, 230);
}

int Appearance::blurDivisor() const
{
    return std::clamp(blur / 2, 2, 16);
}

QColor Appearance::panelColor() const
{
    return dark ? QColor(30, 30, 46, alpha()) : QColor(255, 255, 255, alpha());
}

QColor Appearance::windowColor() const
{
    return dark ? QColor(22, 24, 36) : QColor(244, 246, 250);
}

QColor Appearance::borderColor() const
{
    return dark ? QColor(255, 255, 255, 31) : QColor(255, 255, 255, 102);
}

QColor Appearance::textColor() const
{
    return dark ? QColor(QStringLiteral("#E7F2F6")) : QColor(QStringLiteral("#1C1C1E"));
}

QColor Appearance::mutedColor() const
{
    return dark ? QColor(QStringLiteral("#8FA3B3")) : QColor(QStringLiteral("#6B7280"));
}

QColor Appearance::frostColor() const
{
    return panelColor();
}

QColor Appearance::accentColor() const
{
    return QColor(QStringLiteral("#00E5FF"));
}

Appearance current()
{
    load();
    return gAppearance;
}

void load()
{
    if (gLoaded) {
        return;
    }

    gLoaded = true;
    gAppearance = {};

    QFile file(configPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        const int split = line.indexOf(QLatin1Char('='));

        if (split <= 0) {
            continue;
        }

        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();

        if (key == QLatin1String("dark")) {
            gAppearance.dark = value != QLatin1String("0");
        } else if (key == QLatin1String("blur")) {
            gAppearance.blur = value.toInt();
        } else if (key == QLatin1String("liquid")) {
            gAppearance.liquid = value.toInt();
        }
    }

    gAppearance.clamp();
}

void update(Appearance next)
{
    load();
    next.clamp();

    if (next.dark == gAppearance.dark &&
        next.blur == gAppearance.blur &&
        next.liquid == gAppearance.liquid) {
        return;
    }

    gAppearance = next;

    QSaveFile file(configPath());

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QByteArray body =
            "dark=" + QByteArray(gAppearance.dark ? "1" : "0") + "\n"
            "blur=" + QByteArray::number(gAppearance.blur) + "\n"
            "liquid=" + QByteArray::number(gAppearance.liquid) + "\n";
        file.write(body);
        file.commit();
    }

    if (QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        EdenSnow::apply(app);
    }

    notify();
}

void watch(QObject *owner, std::function<void()> fn)
{
    if (owner == nullptr || !fn) {
        return;
    }

    gWatches.push_back(Watch{owner, std::move(fn)});
}

QString settingsStyleSheet()
{
    const Appearance look = current();
    const QString text = argb(look.textColor());
    const QString muted = argb(look.mutedColor());
    const QString border = argb(look.borderColor());
    const QString panel = argb(look.panelColor());
    const QString field = look.dark
        ? QStringLiteral("rgba(255,255,255,0.06)")
        : QStringLiteral("rgba(255,255,255,0.55)");
    const QString hover = look.dark
        ? QStringLiteral("rgba(255,255,255,0.08)")
        : QStringLiteral("rgba(0,0,0,0.05)");
    const QString selected = QStringLiteral("rgba(0,229,255,0.18)");
    const QString window = argb(look.windowColor());

    return QStringLiteral(
        "SettingsWindow { background: %1; color: %2; }"
        "QLabel { background: transparent; color: %2; }"
        "QLabel#subtitle, QLabel#mutedLabel, QLabel#wallpaperPath { color: %3; }"
        "QLabel#wallpaperPath { font-size: 12px; }"
        "QListWidget#sidebar {"
        "  background: %4; border: 1px solid %5; border-radius: 16px;"
        "  color: %3; outline: none; padding: 8px;"
        "}"
        "QListWidget#sidebar::item {"
        "  padding: 10px 12px; border-radius: 12px; margin: 2px 0;"
        "}"
        "QListWidget#sidebar::item:hover { background: %6; }"
        "QListWidget#sidebar::item:selected {"
        "  background: %7; color: #00E5FF; font-weight: 600;"
        "}"
        "QListWidget#wallpaperGallery {"
        "  background: %4; border: 1px solid %5; border-radius: 12px; padding: 10px;"
        "}"
        "QListWidget#wallpaperGallery::item {"
        "  background: %8; border: 1px solid %5; border-radius: 10px; padding: 6px; margin: 2px;"
        "}"
        "QListWidget#wallpaperGallery::item:selected {"
        "  background: rgba(0,229,255,0.22); border: 1px solid #00E5FF;"
        "}"
        "QLabel#wallpaperPreview {"
        "  background: %4; border: 1px solid %5; border-radius: 14px; color: %3;"
        "}"
        "QFrame#glassCard {"
        "  background: %9; border: 1px solid %5; border-radius: 18px;"
        "}"
        "QFrame#segmentBox {"
        "  background: %8; border: 1px solid %5; border-radius: 14px;"
        "}"
        "QLineEdit#settingsSearch {"
        "  background: %8; color: %2; border: 1px solid %5; border-radius: 12px; padding: 8px 12px;"
        "}"
        "QComboBox {"
        "  background: %8; color: %2; border: 1px solid %5;"
        "  border-radius: 12px; padding: 8px 12px; min-width: 220px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: %1; color: %2; selection-background-color: #00E5FF; selection-color: #121820;"
        "}"
        "QPushButton {"
        "  background: %8; color: %2; border: 1px solid %5;"
        "  border-radius: 12px; padding: 9px 16px;"
        "}"
        "QPushButton:hover { background: rgba(0,229,255,0.16); }"
        "QPushButton#applyButton {"
        "  background: #00E5FF; color: #121820; border: none; font-weight: 600;"
        "}"
        "QPushButton#applyButton:hover { background: #7AF0FF; }"
        "QPushButton#segmentButton {"
        "  background: transparent; border: none; border-radius: 10px; padding: 7px 18px;"
        "}"
        "QPushButton#segmentButton:checked {"
        "  background: %7; color: #00E5FF; font-weight: 600;"
        "}"
        "QPushButton#trafficClose, QPushButton#trafficMin, QPushButton#trafficMax {"
        "  border: none; border-radius: 8px; padding: 0; min-width: 16px; max-width: 16px;"
        "  min-height: 16px; max-height: 16px;"
        "}"
        "QPushButton#trafficClose { background: #FF5F57; }"
        "QPushButton#trafficClose:hover { background: #FF857E; }"
        "QPushButton#trafficMin { background: #FEBC2E; }"
        "QPushButton#trafficMin:hover { background: #FFD060; }"
        "QPushButton#trafficMax { background: #28C840; }"
        "QPushButton#trafficMax:hover { background: #5AD96A; }"
        "QSlider { min-height: 22px; }"
        "QSlider::groove:horizontal {"
        "  height: 6px; background: rgba(127,127,127,0.28); border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 16px; margin: -6px 0; border-radius: 8px; background: #00E5FF;"
        "}"
        "QSlider::sub-page:horizontal { background: #00E5FF; border-radius: 3px; }"
    ).arg(window, text, muted, field, border, hover, selected, field, panel);
}

}  // namespace EdenGlass
