#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

#include <xcb/xcb.h>

class ScreenshotService final : public QObject, public QAbstractNativeEventFilter
{
public:
    static void install();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    ScreenshotService();
    void grabHotkey();
    void capture();

    xcb_timestamp_t lastRelease_ = 0;
    bool haveRelease_ = false;
};
