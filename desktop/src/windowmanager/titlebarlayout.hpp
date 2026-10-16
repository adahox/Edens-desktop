#pragma once

#include <xcb/xcb.h>

#include <cstdint>

constexpr uint16_t kTitleBarHeight = 32;
constexpr uint16_t kButtonSize = 18;
constexpr uint16_t kButtonGap = 6;
constexpr uint16_t kButtonMargin = 8;

enum class TitleButton {
    None,
    Minimize,
    Maximize,
    Close
};

enum class ResizeDirection {
    None = 0,
    Left = 1 << 0,
    Right = 1 << 1,
    Top = 1 << 2,
    Bottom = 1 << 3,
    TopLeft = Top | Left,
    TopRight = Top | Right,
    BottomLeft = Bottom | Left,
    BottomRight = Bottom | Right
};

constexpr uint16_t kResizeMargin = 6;

struct TitleButtonLayout {
    int16_t x = 0;
    int16_t y = 0;
};

struct TitleBarButtons {
    TitleButtonLayout minimize;
    TitleButtonLayout maximize;
    TitleButtonLayout close;
};



inline TitleBarButtons titleBarButtons(uint16_t frameWidth)
{
    const int16_t y = static_cast<int16_t>((kTitleBarHeight - kButtonSize) / 2);
    const int16_t closeX    = static_cast<int16_t>(frameWidth - kButtonMargin - kButtonSize);
    const int16_t maximizeX = static_cast<int16_t>(closeX - kButtonGap - kButtonSize);
    const int16_t minimizeX = static_cast<int16_t>(maximizeX - kButtonGap - kButtonSize);

    return {
        {minimizeX, y},
        {maximizeX, y},
        {closeX, y}
    };
}

inline bool containsButton(const TitleButtonLayout& button, int16_t x, int16_t y)
{
    return x >= button.x &&
           x < button.x + static_cast<int16_t>(kButtonSize) &&
           y >= button.y &&
           y < button.y + static_cast<int16_t>(kButtonSize);
}

inline TitleButton titleButtonAt(uint16_t frameWidth, int16_t x, int16_t y)
{
    if (y < 0 || y >= static_cast<int16_t>(kTitleBarHeight)) {
        return TitleButton::None;
    }

    const TitleBarButtons buttons = titleBarButtons(frameWidth);

    if (containsButton(buttons.close, x, y)) {
        return TitleButton::Close;
    }

    if (containsButton(buttons.maximize, x, y)) {
        return TitleButton::Maximize;
    }

    if (containsButton(buttons.minimize, x, y)) {
        return TitleButton::Minimize;
    }

    return TitleButton::None;
}

inline uint32_t packedCoordinate(int16_t value)
{
    return static_cast<uint32_t>(static_cast<int32_t>(value));
}
