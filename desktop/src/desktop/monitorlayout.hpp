#pragma once

#include <QString>

namespace EdenMonitors {

struct Layout
{
    QString primary;
    QString primaryMode;
    QString secondary;
    QString secondaryMode;
    QString side = QStringLiteral("right");
};

[[nodiscard]] QString configPath();
[[nodiscard]] QString applyScriptPath();

bool save(const Layout &layout);
bool load(Layout *layout);
bool apply(const Layout &layout);
bool applySaved();

}  // namespace EdenMonitors
