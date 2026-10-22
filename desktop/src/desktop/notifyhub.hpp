#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace EdenNotify {

void start();
[[nodiscard]] int countFor(const QString &label, const QString &iconName);
void clear(const QString &label, const QString &iconName);
void watch(QObject *owner, std::function<void()> fn);

}  // namespace EdenNotify
