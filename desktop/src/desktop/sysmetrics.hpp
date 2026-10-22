#pragma once

#include <QString>

namespace EdenMetrics {

struct Snapshot
{
    bool cpuOk = false;
    double cpuPercent = 0;
    bool ramOk = false;
    double ramPercent = 0;
    double uptimeSeconds = 0;
    bool tempOk = false;
    int tempAvgC = 0;
    int tempMaxC = 0;
    double netBytesPerSec = 0;
    int batteryPercent = -1;
    bool batteryCharging = false;
    QString keyboard;
    QString mediaTitle;
};

[[nodiscard]] const Snapshot &current();
[[nodiscard]] QString formatRate(double bytesPerSec);
[[nodiscard]] QString formatDuration(double seconds);

}  // namespace EdenMetrics
