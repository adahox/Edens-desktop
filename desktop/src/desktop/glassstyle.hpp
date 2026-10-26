#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include <functional>

namespace EdenGlass {

struct Appearance
{
    bool dark = false;
    int blur = 20;
    int liquid = 70;

    void clamp();
    [[nodiscard]] int alpha() const;
    [[nodiscard]] int blurDivisor() const;
    [[nodiscard]] QColor panelColor() const;
    [[nodiscard]] QColor windowColor() const;
    [[nodiscard]] QColor borderColor() const;
    [[nodiscard]] QColor textColor() const;
    [[nodiscard]] QColor mutedColor() const;
    [[nodiscard]] QColor frostColor() const;
    [[nodiscard]] QColor accentColor() const;
};

constexpr int kRadius = 18;

[[nodiscard]] Appearance current();
void load();
void update(Appearance next);
void watch(QObject *owner, std::function<void()> fn);

[[nodiscard]] QString settingsStyleSheet();

}  // namespace EdenGlass
