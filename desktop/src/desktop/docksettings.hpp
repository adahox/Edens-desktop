#pragma once

#include <QString>

namespace EdenDock {

struct Settings
{
    int iconSize = 48;
    int iconHoverSize = 64;
    int fontSize = 11;
    int bottomMargin = 6;
    int iconGap = 8;

    void clamp();
};

[[nodiscard]] QString configPath();
[[nodiscard]] Settings defaults();
[[nodiscard]] Settings load();
bool save(const Settings &settings);
void apply(const Settings &settings);

}  // namespace EdenDock
