#pragma once

#include <cstdint>
#include <vector>

struct FrameSize {
    uint16_t width = 1;
    uint16_t height = 1;
};

struct MonitorRect {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t width = 1;
    uint16_t height = 1;
};

inline FrameSize clampFrameSize(
    uint16_t width,
    uint16_t height,
    uint16_t minimumWidth,
    uint16_t minimumHeight,
    uint16_t maximumWidth,
    uint16_t maximumHeight,
    bool honorMaximum)
{
    FrameSize size{width, height};

    if (size.width < minimumWidth) {
        size.width = minimumWidth;
    }

    if (size.height < minimumHeight) {
        size.height = minimumHeight;
    }

    if (honorMaximum && maximumWidth > 0 && size.width > maximumWidth) {
        size.width = maximumWidth;
    }

    if (honorMaximum && maximumHeight > 0 && size.height > maximumHeight) {
        size.height = maximumHeight;
    }

    return size;
}

inline int nextCycleIndex(int current, int count, int direction)
{
    if (count <= 0) {
        return -1;
    }

    if (current < 0 || current >= count) {
        return direction < 0 ? count - 1 : 0;
    }

    int next = current + (direction < 0 ? -1 : 1);

    if (next < 0) {
        next = count - 1;
    }

    if (next >= count) {
        next = 0;
    }

    return next;
}

inline int monitorIndexAt(
    const std::vector<MonitorRect>& monitors,
    int16_t x,
    int16_t y)
{
    for (int index = 0; index < static_cast<int>(monitors.size()); ++index) {
        const MonitorRect& monitor = monitors[static_cast<std::size_t>(index)];
        const int right = monitor.x + static_cast<int>(monitor.width);
        const int bottom = monitor.y + static_cast<int>(monitor.height);

        if (x >= monitor.x && x < right && y >= monitor.y && y < bottom) {
            return index;
        }
    }

    return monitors.empty() ? -1 : 0;
}

inline MonitorRect workAreaFor(MonitorRect monitor, uint16_t panelHeight)
{
    if (monitor.height > panelHeight) {
        monitor.height = static_cast<uint16_t>(monitor.height - panelHeight);
    }

    return monitor;
}
