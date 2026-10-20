#include "windowmanager.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#undef None
#undef Status
#undef Bool
#undef True
#undef False
#undef Success

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kNormalState = 1;
constexpr uint32_t kIconicState = 3;
constexpr uint32_t kNetWmStateRemove = 0;
constexpr uint32_t kNetWmStateAdd = 1;
constexpr uint32_t kNetWmStateToggle = 2;
constexpr uint32_t kAllDesktops = 0xFFFFFFFFu;

constexpr uint32_t kSizeHintMin = 1u << 4;
constexpr uint32_t kSizeHintMax = 1u << 5;
constexpr uint32_t kSizeHintBase = 1u << 8;
constexpr uint32_t kInputHint = 1u;
constexpr char kPanelFont[] =
    "-misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1";

uint32_t allocateColor(
    xcb_connection_t* connection,
    xcb_colormap_t colormap,
    uint32_t fallback,
    uint16_t red,
    uint16_t green,
    uint16_t blue)
{
    const auto cookie = xcb_alloc_color(connection, colormap, red, green, blue);
    xcb_alloc_color_reply_t* reply = xcb_alloc_color_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return fallback;
    }

    const uint32_t pixel = reply->pixel;
    std::free(reply);
    return pixel;
}

bool openPanelFont(xcb_connection_t* connection, xcb_font_t font)
{
    const auto cookie = xcb_open_font_checked(
        connection,
        font,
        static_cast<uint16_t>(std::strlen(kPanelFont)),
        kPanelFont);
    xcb_generic_error_t* error = xcb_request_check(connection, cookie);

    if (error == nullptr) {
        return true;
    }

    std::free(error);
    const auto alias = xcb_open_font_checked(connection, font, 5, "fixed");
    error = xcb_request_check(connection, alias);

    if (error == nullptr) {
        return true;
    }

    std::free(error);
    return false;
}

std::string readableTitle(const char* data, int length, bool utf8)
{
    std::string title;
    title.reserve(static_cast<std::size_t>(std::max(length, 0)));

    for (int index = 0; index < length && title.size() < 48; ++index) {
        const auto byte = static_cast<unsigned char>(data[index]);

        if (byte == 0) {
            break;
        }

        if (utf8 && byte >= 128) {
            continue;
        }

        if (byte >= 32) {
            title.push_back(static_cast<char>(byte));
        }
    }

    if (title.empty()) {
        title = "janela";
    }

    return title;
}

}  // namespace

xcb_atom_t WindowManager::internAtom(const char* name)
{
    if (connection_ == nullptr || name == nullptr) {
        return XCB_ATOM_NONE;
    }

    const auto cookie = xcb_intern_atom(
        connection_,
        0,
        static_cast<uint16_t>(std::strlen(name)),
        name);

    xcb_intern_atom_reply_t* reply =
        xcb_intern_atom_reply(connection_, cookie, nullptr);

    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }

    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

bool WindowManager::hasWindowType(xcb_window_t window, xcb_atom_t type)
{
    if (connection_ == nullptr || type == XCB_ATOM_NONE) {
        return false;
    }

    if (wmWindowType_ == XCB_ATOM_NONE) {
        wmWindowType_ = internAtom("_NET_WM_WINDOW_TYPE");
    }

    if (wmWindowType_ == XCB_ATOM_NONE) {
        return false;
    }

    const auto cookie = xcb_get_property(
        connection_,
        0,
        window,
        wmWindowType_,
        XCB_ATOM_ATOM,
        0,
        32);

    xcb_get_property_reply_t* reply =
        xcb_get_property_reply(connection_, cookie, nullptr);

    if (reply == nullptr) {
        return false;
    }

    const auto* atoms = static_cast<const xcb_atom_t*>(
        xcb_get_property_value(reply));
    const int count = xcb_get_property_value_length(reply) /
                      static_cast<int>(sizeof(xcb_atom_t));

    bool found = false;

    for (int index = 0; index < count; ++index) {
        if (atoms[index] == type) {
            found = true;
            break;
        }
    }

    std::free(reply);
    return found;
}

bool WindowManager::isDockWindow(xcb_window_t window)
{
    if (wmWindowTypeDock_ == XCB_ATOM_NONE) {
        wmWindowTypeDock_ = internAtom("_NET_WM_WINDOW_TYPE_DOCK");
    }

    return hasWindowType(window, wmWindowTypeDock_);
}

void WindowManager::noteDock(xcb_window_t window)
{
    if (!isConnected() || window == panel_) {
        return;
    }

    xcb_map_window(connection_, window);

    const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(
        connection_,
        window,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stackMode);

    if (netWmStrut_ == XCB_ATOM_NONE) {
        netWmStrut_ = internAtom("_NET_WM_STRUT");
    }

    // Rebuild reserved edges from every dock on the root (status bar + dock).
    reservedLeft_ = 0;
    reservedRight_ = 0;
    reservedTop_ = 0;
    reservedBottom_ = 0;

    const auto treeCookie = xcb_query_tree(connection_, screen_->root);
    xcb_query_tree_reply_t* tree =
        xcb_query_tree_reply(connection_, treeCookie, nullptr);

    if (tree != nullptr) {
        xcb_window_t* children = xcb_query_tree_children(tree);
        const int count = xcb_query_tree_children_length(tree);

        for (int index = 0; index < count; ++index) {
            const xcb_window_t child = children[index];

            if (!isDockWindow(child)) {
                continue;
            }

            const auto cookie = xcb_get_property(
                connection_,
                0,
                child,
                netWmStrut_,
                XCB_ATOM_CARDINAL,
                0,
                4);
            xcb_get_property_reply_t* reply =
                xcb_get_property_reply(connection_, cookie, nullptr);

            if (reply == nullptr) {
                continue;
            }

            const int valuesCount = xcb_get_property_value_length(reply) / 4;

            if (valuesCount >= 4) {
                const auto* values = static_cast<const uint32_t*>(
                    xcb_get_property_value(reply));
                reservedLeft_ = static_cast<uint16_t>(
                    std::max<uint32_t>(reservedLeft_, values[0]));
                reservedRight_ = static_cast<uint16_t>(
                    std::max<uint32_t>(reservedRight_, values[1]));
                reservedTop_ = static_cast<uint16_t>(
                    std::max<uint32_t>(reservedTop_, values[2]));
                reservedBottom_ = static_cast<uint16_t>(
                    std::max<uint32_t>(reservedBottom_, values[3]));
            }

            std::free(reply);
        }

        std::free(tree);
    } else {
        // Fallback: read only the notifying window.
        const auto cookie = xcb_get_property(
            connection_,
            0,
            window,
            netWmStrut_,
            XCB_ATOM_CARDINAL,
            0,
            4);
        xcb_get_property_reply_t* reply =
            xcb_get_property_reply(connection_, cookie, nullptr);

        if (reply != nullptr) {
            const int valuesCount = xcb_get_property_value_length(reply) / 4;

            if (valuesCount >= 4) {
                const auto* values = static_cast<const uint32_t*>(
                    xcb_get_property_value(reply));
                reservedLeft_ = static_cast<uint16_t>(values[0]);
                reservedRight_ = static_cast<uint16_t>(values[1]);
                reservedTop_ = static_cast<uint16_t>(values[2]);
                reservedBottom_ = static_cast<uint16_t>(values[3]);
            }

            std::free(reply);
        }
    }

    publishRootProperties();

    for (const xcb_window_t desktop : desktopWindows_) {
        fitDesktopWindow(desktop);
    }

    // Re-fit maximized frames to the new work area.
    for (auto& [client, managed] : windows_) {
        (void)client;

        if (managed.minimized || !managed.maximized) {
            continue;
        }

        const auto cookie = xcb_get_geometry(connection_, managed.frame);
        xcb_get_geometry_reply_t* geometry =
            xcb_get_geometry_reply(connection_, cookie, nullptr);

        if (geometry == nullptr) {
            continue;
        }

        const int16_t anchorX = static_cast<int16_t>(
            geometry->x + static_cast<int16_t>(geometry->width / 2));
        const int16_t anchorY = static_cast<int16_t>(
            geometry->y + static_cast<int16_t>(geometry->height / 2));
        std::free(geometry);

        const MonitorRect area = workAreaAt(anchorX, anchorY);
        layoutFrame(
            managed,
            area.x,
            area.y,
            area.width,
            area.height,
            false);
    }

    revealManagedFrames();
    xcb_flush(connection_);
}

MonitorRect WindowManager::virtualDesktopBounds() const
{
    const std::vector<MonitorRect> monitors = connectedMonitors();

    if (monitors.empty()) {
        return MonitorRect{
            0,
            0,
            screen_ == nullptr ? uint16_t{1} : screen_->width_in_pixels,
            screen_ == nullptr ? uint16_t{1} : screen_->height_in_pixels
        };
    }

    int minX = monitors.front().x;
    int minY = monitors.front().y;
    int maxX = monitors.front().x + static_cast<int>(monitors.front().width);
    int maxY = monitors.front().y + static_cast<int>(monitors.front().height);

    for (const MonitorRect& monitor : monitors) {
        minX = std::min(minX, static_cast<int>(monitor.x));
        minY = std::min(minY, static_cast<int>(monitor.y));
        maxX = std::max(
            maxX,
            monitor.x + static_cast<int>(monitor.width));
        maxY = std::max(
            maxY,
            monitor.y + static_cast<int>(monitor.height));
    }

    return MonitorRect{
        static_cast<int16_t>(minX),
        static_cast<int16_t>(minY),
        static_cast<uint16_t>(std::max(1, maxX - minX)),
        static_cast<uint16_t>(std::max(1, maxY - minY))
    };
}

MonitorRect WindowManager::workArea() const
{
    MonitorRect area = virtualDesktopBounds();

    const uint32_t horizontal =
        static_cast<uint32_t>(reservedLeft_) + reservedRight_;
    const uint32_t vertical =
        static_cast<uint32_t>(reservedTop_) + reservedBottom_;

    if (area.width > horizontal) {
        area.x = static_cast<int16_t>(area.x + static_cast<int16_t>(reservedLeft_));
        area.width = static_cast<uint16_t>(area.width - horizontal);
    }

    if (area.height > vertical) {
        area.y = static_cast<int16_t>(area.y + static_cast<int16_t>(reservedTop_));
        area.height = static_cast<uint16_t>(area.height - vertical);
    }

    return area;
}

std::vector<MonitorRect> WindowManager::connectedMonitors() const
{
    std::vector<MonitorRect> monitors;

    Display* display = XOpenDisplay(nullptr);

    if (display != nullptr) {
        int count = 0;
        XRRMonitorInfo* info = XRRGetMonitors(
            display,
            DefaultRootWindow(display),
            1,
            &count);

        if (info != nullptr) {
            monitors.reserve(static_cast<std::size_t>(std::max(count, 0)));

            for (int index = 0; index < count; ++index) {
                if (info[index].width <= 0 || info[index].height <= 0) {
                    continue;
                }

                monitors.push_back(MonitorRect{
                    static_cast<int16_t>(info[index].x),
                    static_cast<int16_t>(info[index].y),
                    static_cast<uint16_t>(info[index].width),
                    static_cast<uint16_t>(info[index].height)
                });
            }

            XRRFreeMonitors(info);
        }

        XCloseDisplay(display);
    }

    if (monitors.empty() && screen_ != nullptr) {
        monitors.push_back(MonitorRect{
            0,
            0,
            screen_->width_in_pixels,
            screen_->height_in_pixels
        });
    }

    return monitors;
}

MonitorRect WindowManager::workAreaAt(int16_t x, int16_t y) const
{
    const std::vector<MonitorRect> monitors = connectedMonitors();
    const int index = monitorIndexAt(monitors, x, y);

    if (index < 0) {
        return workArea();
    }

    const MonitorRect bounds = virtualDesktopBounds();
    const int absoluteRight =
        bounds.x + static_cast<int>(bounds.width);
    const int absoluteBottom =
        bounds.y + static_cast<int>(bounds.height);

    return clampedWorkArea(
        monitors[static_cast<std::size_t>(index)],
        absoluteRight,
        absoluteBottom,
        reservedLeft_,
        reservedRight_,
        reservedTop_,
        reservedBottom_);
}

void WindowManager::refreshScreenLayoutIfNeeded()
{
    if (!isConnected()) {
        return;
    }

    const MonitorRect bounds = virtualDesktopBounds();

    if (hasCachedVirtualBounds_ &&
        cachedVirtualBounds_.x == bounds.x &&
        cachedVirtualBounds_.y == bounds.y &&
        cachedVirtualBounds_.width == bounds.width &&
        cachedVirtualBounds_.height == bounds.height) {
        return;
    }

    cachedVirtualBounds_ = bounds;
    hasCachedVirtualBounds_ = true;
    refitAfterScreenChange();
}

void WindowManager::refitAfterScreenChange()
{
    if (!isConnected()) {
        return;
    }

    publishRootProperties();

    for (const xcb_window_t desktop : desktopWindows_) {
        fitDesktopWindow(desktop);
    }

    for (auto& [client, window] : windows_) {
        (void)client;

        if (window.minimized || !window.maximized) {
            continue;
        }

        const auto cookie = xcb_get_geometry(connection_, window.frame);
        xcb_get_geometry_reply_t* geometry =
            xcb_get_geometry_reply(connection_, cookie, nullptr);

        if (geometry == nullptr) {
            continue;
        }

        const int16_t anchorX = static_cast<int16_t>(
            geometry->x + static_cast<int16_t>(geometry->width / 2));
        const int16_t anchorY = static_cast<int16_t>(
            geometry->y + static_cast<int16_t>(geometry->height / 2));
        std::free(geometry);

        const MonitorRect area = workAreaAt(anchorX, anchorY);
        layoutFrame(
            window,
            area.x,
            area.y,
            area.width,
            area.height,
            false);
    }

    revealManagedFrames();
    xcb_flush(connection_);
}

void WindowManager::revealManagedFrames()
{
    if (!isConnected()) {
        return;
    }

    const MonitorRect area = workArea();
    std::vector<xcb_window_t> clients;
    clients.reserve(windows_.size());

    for (const auto& [client, window] : windows_) {
        (void)window;
        clients.push_back(client);
    }

    for (const xcb_window_t client : clients) {
        const auto iterator = windows_.find(client);

        if (iterator == windows_.end() || iterator->second.minimized) {
            continue;
        }

        ManagedWindow& window = iterator->second;
        const auto cookie = xcb_get_geometry(connection_, window.frame);
        xcb_get_geometry_reply_t* geometry =
            xcb_get_geometry_reply(connection_, cookie, nullptr);

        if (geometry == nullptr) {
            continue;
        }

        const int16_t x = geometry->x;
        const int16_t y = geometry->y;
        const uint16_t width = geometry->width;
        const uint16_t height = geometry->height;
        std::free(geometry);

        if (y < area.y) {
            layoutFrame(window, x, area.y, width, height, !window.maximized);
        }
    }
}

void WindowManager::readWindowHints(ManagedWindow& window)
{
    if (!isConnected()) {
        return;
    }

    if (wmNormalHints_ == XCB_ATOM_NONE) {
        wmNormalHints_ = internAtom("WM_NORMAL_HINTS");
    }

    const auto hintsCookie = xcb_get_property(
        connection_,
        0,
        window.client,
        wmNormalHints_,
        XCB_GET_PROPERTY_TYPE_ANY,
        0,
        32);

    xcb_get_property_reply_t* hints =
        xcb_get_property_reply(connection_, hintsCookie, nullptr);

    if (hints != nullptr) {
        const auto* values = static_cast<const uint32_t*>(
            xcb_get_property_value(hints));
        const int count = xcb_get_property_value_length(hints) / 4;

        if (count > 0) {
            const uint32_t flags = values[0];

            const uint16_t chrome = clientTopOffset(window.decorated);

            if ((flags & kSizeHintMin) != 0 && count > 6) {
                window.minimumWidth = static_cast<uint16_t>(
                    std::max<uint32_t>(1, values[5]));
                window.minimumHeight = static_cast<uint16_t>(std::max<uint32_t>(
                    chrome + 1,
                    values[6] + chrome));
            } else if ((flags & kSizeHintBase) != 0 && count > 16) {
                window.minimumWidth = static_cast<uint16_t>(
                    std::max<uint32_t>(1, values[15]));
                window.minimumHeight = static_cast<uint16_t>(std::max<uint32_t>(
                    chrome + 1,
                    values[16] + chrome));
            }

            if ((flags & kSizeHintMax) != 0 && count > 8 &&
                values[7] > 0 && values[8] > 0) {
                window.maximumWidth = static_cast<uint16_t>(
                    std::min<uint32_t>(values[7], 65535));
                window.maximumHeight = static_cast<uint16_t>(std::min<uint32_t>(
                    values[8] + chrome,
                    65535));
            }
        }

        std::free(hints);
    }

    const auto inputCookie = xcb_get_property(
        connection_,
        0,
        window.client,
        XCB_ATOM_WM_HINTS,
        XCB_ATOM_WM_HINTS,
        0,
        9);

    xcb_get_property_reply_t* input =
        xcb_get_property_reply(connection_, inputCookie, nullptr);

    if (input != nullptr) {
        const auto* values = static_cast<const uint32_t*>(
            xcb_get_property_value(input));
        const int count = xcb_get_property_value_length(input) / 4;

        if (count > 1 && (values[0] & kInputHint) != 0) {
            window.acceptsInput = values[1] != 0;
        }

        std::free(input);
    }

    const auto transientCookie = xcb_get_property(
        connection_,
        0,
        window.client,
        XCB_ATOM_WM_TRANSIENT_FOR,
        XCB_ATOM_WINDOW,
        0,
        1);

    xcb_get_property_reply_t* transient =
        xcb_get_property_reply(connection_, transientCookie, nullptr);

    if (transient != nullptr) {
        if (xcb_get_property_value_length(transient) >= static_cast<int>(sizeof(xcb_window_t))) {
            window.transientFor = *static_cast<const xcb_window_t*>(
                xcb_get_property_value(transient));
        }

        std::free(transient);
    }

    const auto nameCookie = xcb_get_property(
        connection_,
        0,
        window.client,
        XCB_ATOM_WM_NAME,
        XCB_GET_PROPERTY_TYPE_ANY,
        0,
        64);

    xcb_get_property_reply_t* name =
        xcb_get_property_reply(connection_, nameCookie, nullptr);

    if (name != nullptr && xcb_get_property_value_length(name) > 0) {
        window.title = readableTitle(
            static_cast<const char*>(xcb_get_property_value(name)),
            xcb_get_property_value_length(name),
            false);
    }

    std::free(name);

    if (window.title.empty()) {
        if (netWmName_ == XCB_ATOM_NONE) {
            netWmName_ = internAtom("_NET_WM_NAME");
        }
    }

    if (window.title.empty() && netWmName_ != XCB_ATOM_NONE) {
        const auto utf8Cookie = xcb_get_property(
            connection_,
            0,
            window.client,
            netWmName_,
            XCB_GET_PROPERTY_TYPE_ANY,
            0,
            64);

        xcb_get_property_reply_t* utf8 =
            xcb_get_property_reply(connection_, utf8Cookie, nullptr);

        if (utf8 != nullptr && xcb_get_property_value_length(utf8) > 0) {
            window.title = readableTitle(
                static_cast<const char*>(xcb_get_property_value(utf8)),
                xcb_get_property_value_length(utf8),
                true);
        }

        std::free(utf8);
    }
}

void WindowManager::placeTransient(ManagedWindow& window)
{
    if (!isConnected()) {
        return;
    }

    const auto frameCookie = xcb_get_geometry(connection_, window.frame);
    xcb_get_geometry_reply_t* frame =
        xcb_get_geometry_reply(connection_, frameCookie, nullptr);

    if (frame == nullptr) {
        return;
    }

    ManagedWindow* parent = nullptr;

    if (window.transientFor != XCB_WINDOW_NONE) {
        const auto iterator = windows_.find(window.transientFor);

        if (iterator != windows_.end()) {
            parent = &iterator->second;
        }
    }

    if (parent != nullptr) {
        window.desktop = parent->desktop;

        const auto parentCookie = xcb_get_geometry(connection_, parent->frame);
        xcb_get_geometry_reply_t* parentGeometry =
            xcb_get_geometry_reply(connection_, parentCookie, nullptr);

        if (parentGeometry != nullptr) {
            const int16_t x = static_cast<int16_t>(
                parentGeometry->x +
                (static_cast<int>(parentGeometry->width) - frame->width) / 2);
            const int16_t y = static_cast<int16_t>(
                parentGeometry->y +
                (static_cast<int>(parentGeometry->height) - frame->height) / 2);

            layoutFrame(window, x, y, frame->width, frame->height, true);
            std::free(parentGeometry);
        }

        const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(
            connection_,
            window.frame,
            XCB_CONFIG_WINDOW_STACK_MODE,
            stackMode);
    } else {
        if (wmWindowTypeDialog_ == XCB_ATOM_NONE) {
            wmWindowTypeDialog_ = internAtom("_NET_WM_WINDOW_TYPE_DIALOG");
        }

        if (!hasWindowType(window.client, wmWindowTypeDialog_)) {
            std::free(frame);
            return;
        }

        const MonitorRect area = workArea();
        const int16_t x = static_cast<int16_t>(
            area.x + (static_cast<int>(area.width) - frame->width) / 2);
        const int16_t y = static_cast<int16_t>(
            area.y + (static_cast<int>(area.height) - frame->height) / 2);
        layoutFrame(window, x, y, frame->width, frame->height, true);
    }

    if (!window.sticky && window.desktop != currentDesktop_) {
        xcb_unmap_window(connection_, window.frame);
    }

    std::free(frame);
}

void WindowManager::publishRootProperties()
{
    if (!isConnected() || netSupported_ == XCB_ATOM_NONE) {
        return;
    }

    const xcb_atom_t supported[] = {
        netSupported_,
        netSupportingWmCheck_,
        netWmName_,
        netClientList_,
        netActiveWindow_,
        netWmState_,
        netWmStateMaximizedVert_,
        netWmStateMaximizedHorz_,
        netWmStateHidden_,
        netWmStateFullscreen_,
        netCloseWindow_,
        netCurrentDesktop_,
        netNumberOfDesktops_,
        netWmDesktop_,
        netFrameExtents_,
        netWorkarea_,
        netDesktopGeometry_,
        netDesktopViewport_,
        netWmStrut_,
        netWmStrutPartial_,
        netWmMoveResize_,
        wmProtocols_,
        wmDeleteWindow_,
        wmTakeFocus_
    };

    std::vector<xcb_atom_t> present;

    for (const xcb_atom_t atom : supported) {
        if (atom != XCB_ATOM_NONE) {
            present.push_back(atom);
        }
    }

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netSupported_,
        XCB_ATOM_ATOM,
        32,
        static_cast<uint32_t>(present.size()),
        present.data());

    if (supportingWindow_ != XCB_WINDOW_NONE) {
        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            screen_->root,
            netSupportingWmCheck_,
            XCB_ATOM_WINDOW,
            32,
            1,
            &supportingWindow_);
        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            supportingWindow_,
            netSupportingWmCheck_,
            XCB_ATOM_WINDOW,
            32,
            1,
            &supportingWindow_);
    }

    const char name[] = "Eden";

    if (utf8String_ != XCB_ATOM_NONE && netWmName_ != XCB_ATOM_NONE) {
        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            screen_->root,
            netWmName_,
            utf8String_,
            8,
            sizeof(name) - 1,
            name);

        if (supportingWindow_ != XCB_WINDOW_NONE) {
            xcb_change_property(
                connection_,
                XCB_PROP_MODE_REPLACE,
                supportingWindow_,
                netWmName_,
                utf8String_,
                8,
                sizeof(name) - 1,
                name);
        }
    }

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netCurrentDesktop_,
        XCB_ATOM_CARDINAL,
        32,
        1,
        &currentDesktop_);

    const uint32_t desktopCount = kDesktopCount;

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netNumberOfDesktops_,
        XCB_ATOM_CARDINAL,
        32,
        1,
        &desktopCount);

    const MonitorRect bounds = virtualDesktopBounds();
    const uint32_t geometry[] = {
        bounds.width,
        bounds.height
    };

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netDesktopGeometry_,
        XCB_ATOM_CARDINAL,
        32,
        2,
        geometry);

    const uint32_t viewport[] = {0, 0};

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netDesktopViewport_,
        XCB_ATOM_CARDINAL,
        32,
        2,
        viewport);

    const MonitorRect area = workArea();
    uint32_t workAreas[kDesktopCount * 4];

    for (uint32_t desktop = 0; desktop < kDesktopCount; ++desktop) {
        workAreas[desktop * 4] = static_cast<uint32_t>(area.x);
        workAreas[desktop * 4 + 1] = static_cast<uint32_t>(area.y);
        workAreas[desktop * 4 + 2] = area.width;
        workAreas[desktop * 4 + 3] = area.height;
    }

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netWorkarea_,
        XCB_ATOM_CARDINAL,
        32,
        kDesktopCount * 4,
        workAreas);
}

void WindowManager::publishClientList()
{
    if (!isConnected() || netClientList_ == XCB_ATOM_NONE) {
        return;
    }

    std::vector<xcb_window_t> clients;
    clients.reserve(windows_.size());

    for (const xcb_window_t client : focusOrder_) {
        if (windows_.contains(client)) {
            clients.push_back(client);
        }
    }

    for (const auto& [client, window] : windows_) {
        (void)window;

        if (std::find(clients.begin(), clients.end(), client) == clients.end()) {
            clients.push_back(client);
        }
    }

    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        screen_->root,
        netClientList_,
        XCB_ATOM_WINDOW,
        32,
        static_cast<uint32_t>(clients.size()),
        clients.empty() ? nullptr : clients.data());

    xcb_window_t active = XCB_WINDOW_NONE;

    if (const ManagedWindow* focused = findByFrame(focusedFrame_)) {
        active = focused->client;
    }

    if (netActiveWindow_ != XCB_ATOM_NONE) {
        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            screen_->root,
            netActiveWindow_,
            XCB_ATOM_WINDOW,
            32,
            1,
            &active);
    }
}

void WindowManager::publishClientState(const ManagedWindow& window)
{
    if (!isConnected()) {
        return;
    }

    if (wmState_ != XCB_ATOM_NONE) {
        const uint32_t state[] = {
            window.minimized ? kIconicState : kNormalState,
            XCB_WINDOW_NONE
        };

        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            window.client,
            wmState_,
            wmState_,
            32,
            2,
            state);
    }

    if (netWmState_ != XCB_ATOM_NONE) {
        xcb_atom_t states[4];
        uint32_t count = 0;

        if (window.maximized) {
            if (netWmStateMaximizedVert_ != XCB_ATOM_NONE) {
                states[count++] = netWmStateMaximizedVert_;
            }

            if (netWmStateMaximizedHorz_ != XCB_ATOM_NONE) {
                states[count++] = netWmStateMaximizedHorz_;
            }
        }

        if (window.minimized && netWmStateHidden_ != XCB_ATOM_NONE) {
            states[count++] = netWmStateHidden_;
        }

        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            window.client,
            netWmState_,
            XCB_ATOM_ATOM,
            32,
            count,
            count == 0 ? nullptr : states);
    }

    if (netWmDesktop_ != XCB_ATOM_NONE) {
        const uint32_t desktop = window.sticky ? kAllDesktops : window.desktop;

        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            window.client,
            netWmDesktop_,
            XCB_ATOM_CARDINAL,
            32,
            1,
            &desktop);
    }

    if (netFrameExtents_ != XCB_ATOM_NONE) {
        const uint32_t top = clientTopOffset(window.decorated);
        const uint32_t side = kFrameBorderWidth;
        const uint32_t extents[] = {side, side, top + side, side};

        xcb_change_property(
            connection_,
            XCB_PROP_MODE_REPLACE,
            window.client,
            netFrameExtents_,
            XCB_ATOM_CARDINAL,
            32,
            4,
            extents);
    }
}

bool WindowManager::clientWantsTitleBar(xcb_window_t client) const
{
    if (!isConnected() || client == XCB_WINDOW_NONE) {
        return true;
    }

    if (motifWmHints_ != XCB_ATOM_NONE) {
        const auto cookie = xcb_get_property(
            connection_,
            0,
            client,
            motifWmHints_,
            XCB_GET_PROPERTY_TYPE_ANY,
            0,
            5);
        xcb_get_property_reply_t* reply =
            xcb_get_property_reply(connection_, cookie, nullptr);

        if (reply != nullptr) {
            const int count = xcb_get_property_value_length(reply) / 4;

            if (count >= 3) {
                const auto* values = static_cast<const uint32_t*>(
                    xcb_get_property_value(reply));
                const bool hasDecorFlag =
                    (values[0] & kMotifHintsDecorations) != 0;
                const bool wants = motifWantsTitleBar(hasDecorFlag, values[2]);
                std::free(reply);

                if (hasDecorFlag) {
                    return wants;
                }
            } else {
                std::free(reply);
            }
        }
    }

    // GTK/Electron CSD windows advertise frame extents they draw themselves.
    if (gtkFrameExtents_ != XCB_ATOM_NONE) {
        const auto cookie = xcb_get_property(
            connection_,
            0,
            client,
            gtkFrameExtents_,
            XCB_ATOM_CARDINAL,
            0,
            4);
        xcb_get_property_reply_t* reply =
            xcb_get_property_reply(connection_, cookie, nullptr);

        if (reply != nullptr) {
            const int bytes = xcb_get_property_value_length(reply);
            std::free(reply);

            if (bytes >= 16) {
                return false;
            }
        }
    }

    return true;
}

void WindowManager::updateFrameBorder(
    const ManagedWindow& window,
    bool focused) const
{
    if (!isConnected() || window.frame == XCB_WINDOW_NONE) {
        return;
    }

    const uint32_t pixel =
        focused ? borderFocusedPixel_ : borderUnfocusedPixel_;
    xcb_change_window_attributes(
        connection_,
        window.frame,
        XCB_CW_BORDER_PIXEL,
        &pixel);
}

void WindowManager::applyDecorationMode(ManagedWindow& window, bool decorated)
{
    if (!isConnected()) {
        return;
    }

    const auto cookie = xcb_get_geometry(connection_, window.frame);
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, cookie, nullptr);

    if (geometry == nullptr) {
        window.decorated = decorated;
        return;
    }

    const uint16_t oldChrome = clientTopOffset(window.decorated);
    const uint16_t newChrome = clientTopOffset(decorated);
    const uint16_t clientHeight =
        geometry->height > oldChrome
            ? static_cast<uint16_t>(geometry->height - oldChrome)
            : static_cast<uint16_t>(1);
    const int16_t x = geometry->x;
    const int16_t y = geometry->y;
    const uint16_t width = geometry->width;
    std::free(geometry);

    window.decorated = decorated;

    if (decorated) {
        xcb_map_window(connection_, window.titleBar);
        const uint32_t backPixel = screen_->black_pixel;
        xcb_change_window_attributes(
            connection_,
            window.frame,
            XCB_CW_BACK_PIXEL,
            &backPixel);
    } else {
        xcb_unmap_window(connection_, window.titleBar);
        const uint32_t noPixmap = XCB_NONE;
        xcb_change_window_attributes(
            connection_,
            window.frame,
            XCB_CW_BACK_PIXMAP,
            &noPixmap);
    }

    layoutFrame(
        window,
        x,
        y,
        width,
        frameHeightForClient(clientHeight, decorated),
        !window.maximized);
    publishClientState(window);

    if (decorated) {
        drawTitleBar(window.titleBar);
    }
}

void WindowManager::rememberFocus(xcb_window_t client)
{
    std::erase(focusOrder_, client);
    focusOrder_.insert(focusOrder_.begin(), client);
}

void WindowManager::forgetFocus(xcb_window_t client)
{
    std::erase(focusOrder_, client);
}

void WindowManager::noteWindowRemoved(xcb_window_t client, xcb_window_t frame)
{
    forgetFocus(client);
    const bool wasFocused = focusedFrame_ == frame;

    if (wasFocused) {
        clearFocus();
    }

    publishClientList();

    if (wasFocused) {
        focusAnotherVisible(client);
    }

    drawPanel();
}

void WindowManager::raiseTransients(xcb_window_t client)
{
    const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};

    for (auto& [id, other] : windows_) {
        (void)id;

        if (other.transientFor != client || other.minimized) {
            continue;
        }

        if (!other.sticky && other.desktop != currentDesktop_) {
            continue;
        }

        xcb_configure_window(
            connection_,
            other.frame,
            XCB_CONFIG_WINDOW_STACK_MODE,
            stackMode);
    }
}

void WindowManager::raisePanel()
{
    if (!isConnected() || panel_ == XCB_WINDOW_NONE) {
        return;
    }

    const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(
        connection_,
        panel_,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stackMode);
}

void WindowManager::drawPanel()
{
    if (!isConnected() || panel_ == XCB_WINDOW_NONE) {
        return;
    }

    const auto geometryCookie = xcb_get_geometry(connection_, panel_);
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, geometryCookie, nullptr);

    if (geometry == nullptr) {
        return;
    }

    const xcb_gcontext_t gc = xcb_generate_id(connection_);
    const xcb_font_t font = xcb_generate_id(connection_);
    const uint32_t fillColor = allocateColor(
        connection_,
        screen_->default_colormap,
        screen_->black_pixel,
        0x1E1E,
        0x2838,
        0x3A3A);
    const uint32_t textColor = allocateColor(
        connection_,
        screen_->default_colormap,
        screen_->white_pixel,
        0xFFFF,
        0xFFFF,
        0xFFFF);
    const bool haveFont = openPanelFont(connection_, font);
    uint32_t values[] = {
        fillColor,
        fillColor,
        haveFont ? font : 0,
        0
    };
    uint32_t mask =
        XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;

    if (haveFont) {
        mask |= XCB_GC_FONT;
    }

    xcb_create_gc(connection_, gc, panel_, mask, values);

    const xcb_rectangle_t background = {
        0,
        0,
        geometry->width,
        geometry->height
    };

    xcb_poly_fill_rectangle(connection_, panel_, gc, 1, &background);

    xcb_change_gc(connection_, gc, XCB_GC_FOREGROUND, &textColor);

    const xcb_point_t separator[] = {
        {0, 0},
        {static_cast<int16_t>(geometry->width), 0}
    };
    xcb_poly_line(connection_, XCB_COORD_MODE_ORIGIN, panel_, gc, 2, separator);

    std::vector<xcb_window_t> ordered;

    for (const xcb_window_t client : focusOrder_) {
        const auto iterator = windows_.find(client);

        if (iterator == windows_.end()) {
            continue;
        }

        if (iterator->second.sticky || iterator->second.desktop == currentDesktop_) {
            ordered.push_back(client);
        }
    }

    for (const auto& [client, window] : windows_) {
        if (!window.sticky && window.desktop != currentDesktop_) {
            continue;
        }

        if (std::find(ordered.begin(), ordered.end(), client) == ordered.end()) {
            ordered.push_back(client);
        }
    }

    const int desktopZone = static_cast<int>(geometry->width) -
                            static_cast<int>(kDesktopCount * 28);
    const int slots = std::max(0, desktopZone / 160);
    panelClients_.clear();

    for (int index = 0; index < slots && index < static_cast<int>(ordered.size()); ++index) {
        panelClients_.push_back(ordered[static_cast<std::size_t>(index)]);
        const ManagedWindow& window = windows_.at(panelClients_.back());
        std::string label = window.frame == focusedFrame_ ? ">" : " ";

        if (window.minimized) {
            label += "[";
            label += window.title;
            label += "]";
        } else {
            label += window.title;
        }

        if (label.size() > 20) {
            label.resize(20);
        }

        if (haveFont) {
            xcb_image_text_8(
                connection_,
                static_cast<uint8_t>(label.size()),
                panel_,
                gc,
                static_cast<int16_t>(8 + index * 160),
                20,
                label.c_str());
        }
    }

    for (uint32_t desktop = 0; desktop < kDesktopCount; ++desktop) {
        char label[8] = {};
        const int length = desktop == currentDesktop_
            ? std::snprintf(label, sizeof(label), "[%u]", desktop + 1)
            : std::snprintf(label, sizeof(label), " %u ", desktop + 1);

        if (haveFont && length > 0) {
            xcb_image_text_8(
                connection_,
                static_cast<uint8_t>(length),
                panel_,
                gc,
                static_cast<int16_t>(desktopZone + 4 + static_cast<int>(desktop) * 28),
                20,
                label);
        }
    }

    xcb_free_gc(connection_, gc);

    if (haveFont) {
        xcb_close_font(connection_, font);
    }
    std::free(geometry);
    raisePanel();
    xcb_flush(connection_);
}

void WindowManager::handlePanelPress(int16_t x)
{
    if (!isConnected()) {
        return;
    }

    const int desktopZone = static_cast<int>(screen_->width_in_pixels) -
                            static_cast<int>(kDesktopCount * 28);

    if (x >= desktopZone) {
        const int index = (x - desktopZone) / 28;

        if (index >= 0 && index < static_cast<int>(kDesktopCount)) {
            switchDesktop(static_cast<uint32_t>(index));
        }

        return;
    }

    const int index = x / 160;

    if (index < 0 || index >= static_cast<int>(panelClients_.size())) {
        return;
    }

    const auto iterator = windows_.find(panelClients_[static_cast<std::size_t>(index)]);

    if (iterator == windows_.end()) {
        return;
    }

    if (!iterator->second.sticky && iterator->second.desktop != currentDesktop_) {
        switchDesktop(iterator->second.desktop);
    }

    focusWindow(iterator->second, XCB_CURRENT_TIME);
}

void WindowManager::cycleFocus(int direction)
{
    std::vector<xcb_window_t> cycle;

    for (const xcb_window_t client : focusOrder_) {
        const auto iterator = windows_.find(client);

        if (iterator == windows_.end()) {
            continue;
        }

        if (iterator->second.sticky || iterator->second.desktop == currentDesktop_) {
            cycle.push_back(client);
        }
    }

    for (const auto& [client, window] : windows_) {
        if (!window.sticky && window.desktop != currentDesktop_) {
            continue;
        }

        if (std::find(cycle.begin(), cycle.end(), client) == cycle.end()) {
            cycle.push_back(client);
        }
    }

    if (cycle.empty()) {
        return;
    }

    int current = -1;

    for (int index = 0; index < static_cast<int>(cycle.size()); ++index) {
        if (windows_.at(cycle[static_cast<std::size_t>(index)]).frame == focusedFrame_) {
            current = index;
        }
    }

    const int next = nextCycleIndex(current, static_cast<int>(cycle.size()), direction);

    if (next < 0) {
        return;
    }

    focusWindow(windows_.at(cycle[static_cast<std::size_t>(next)]), XCB_CURRENT_TIME);
}

void WindowManager::focusAnotherVisible(xcb_window_t exceptClient)
{
    for (const xcb_window_t client : focusOrder_) {
        if (client == exceptClient) {
            continue;
        }

        const auto iterator = windows_.find(client);

        if (iterator == windows_.end() || iterator->second.minimized) {
            continue;
        }

        if (!iterator->second.sticky && iterator->second.desktop != currentDesktop_) {
            continue;
        }

        focusWindow(iterator->second, XCB_CURRENT_TIME);
        return;
    }
}

void WindowManager::switchDesktop(uint32_t desktop)
{
    if (!isConnected() || desktop >= kDesktopCount) {
        return;
    }

    if (desktop == currentDesktop_) {
        drawPanel();
        return;
    }

    currentDesktop_ = desktop;
    bool focusedStillVisible = false;

    for (auto& [client, window] : windows_) {
        (void)client;
        const bool visible = !window.minimized &&
                             (window.sticky || window.desktop == currentDesktop_);

        if (visible) {
            xcb_map_window(connection_, window.frame);

            if (window.frame == focusedFrame_) {
                focusedStillVisible = true;
            }
        } else {
            if (window.frame == focusedFrame_) {
                focusedStillVisible = false;
            }

            xcb_unmap_window(connection_, window.frame);
        }
    }

    xcb_window_t candidate = XCB_WINDOW_NONE;

    for (const xcb_window_t client : focusOrder_) {
        const auto iterator = windows_.find(client);

        if (iterator == windows_.end() || iterator->second.minimized) {
            continue;
        }

        if (iterator->second.sticky || iterator->second.desktop == currentDesktop_) {
            candidate = client;
            break;
        }
    }

    publishRootProperties();

    if (!focusedStillVisible) {
        clearFocus();

        if (candidate != XCB_WINDOW_NONE) {
            focusWindow(windows_.at(candidate), XCB_CURRENT_TIME);
        }
    }

    drawPanel();
    xcb_flush(connection_);
}

void WindowManager::restoreWindow(ManagedWindow& window)
{
    focusWindow(window, XCB_CURRENT_TIME);
}

void WindowManager::applyNetWmState(
    ManagedWindow& window,
    uint32_t action,
    xcb_atom_t atom)
{
    if (atom == XCB_ATOM_NONE || atom != netWmStateHidden_) {
        return;
    }

    const bool hidden = window.minimized;
    const bool wantHidden = action == kNetWmStateAdd ||
                            (action == kNetWmStateToggle && !hidden);

    if (action == kNetWmStateRemove || (action == kNetWmStateToggle && hidden)) {
        if (window.minimized) {
            restoreWindow(window);
        }

        return;
    }

    if (wantHidden && !window.minimized) {
        minimizeWindow(window);
    }
}

void WindowManager::sendConfigureNotify(
    const ManagedWindow& window,
    int16_t frameX,
    int16_t frameY,
    uint16_t frameWidth,
    uint16_t frameHeight)
{
    const uint16_t chrome = clientTopOffset(window.decorated);
    xcb_configure_notify_event_t notify;
    std::memset(&notify, 0, sizeof(notify));
    notify.response_type = XCB_CONFIGURE_NOTIFY;
    notify.event = window.client;
    notify.window = window.client;
    notify.x = frameX;
    notify.y = static_cast<int16_t>(frameY + chrome);
    notify.width = frameWidth;
    notify.height = frameHeight > chrome
        ? static_cast<uint16_t>(frameHeight - chrome)
        : static_cast<uint16_t>(1);
    notify.border_width = 0;
    notify.above_sibling = XCB_WINDOW_NONE;
    notify.override_redirect = 0;

    xcb_send_event(
        connection_,
        0,
        window.client,
        XCB_EVENT_MASK_STRUCTURE_NOTIFY,
        reinterpret_cast<const char*>(&notify));
}

void WindowManager::setupDesktopIntegration()
{
    if (!isConnected()) {
        return;
    }

    netSupported_ = internAtom("_NET_SUPPORTED");
    netSupportingWmCheck_ = internAtom("_NET_SUPPORTING_WM_CHECK");
    netWmName_ = internAtom("_NET_WM_NAME");
    netClientList_ = internAtom("_NET_CLIENT_LIST");
    netActiveWindow_ = internAtom("_NET_ACTIVE_WINDOW");
    netWmState_ = internAtom("_NET_WM_STATE");
    netWmStateMaximizedVert_ = internAtom("_NET_WM_STATE_MAXIMIZED_VERT");
    netWmStateMaximizedHorz_ = internAtom("_NET_WM_STATE_MAXIMIZED_HORZ");
    netWmStateHidden_ = internAtom("_NET_WM_STATE_HIDDEN");
    netWmStateFullscreen_ = internAtom("_NET_WM_STATE_FULLSCREEN");
    netCloseWindow_ = internAtom("_NET_CLOSE_WINDOW");
    netCurrentDesktop_ = internAtom("_NET_CURRENT_DESKTOP");
    netNumberOfDesktops_ = internAtom("_NET_NUMBER_OF_DESKTOPS");
    netWmDesktop_ = internAtom("_NET_WM_DESKTOP");
    netFrameExtents_ = internAtom("_NET_FRAME_EXTENTS");
    netWorkarea_ = internAtom("_NET_WORKAREA");
    netDesktopGeometry_ = internAtom("_NET_DESKTOP_GEOMETRY");
    netDesktopViewport_ = internAtom("_NET_DESKTOP_VIEWPORT");
    netWmStrut_ = internAtom("_NET_WM_STRUT");
    netWmStrutPartial_ = internAtom("_NET_WM_STRUT_PARTIAL");
    utf8String_ = internAtom("UTF8_STRING");
    wmState_ = internAtom("WM_STATE");
    wmChangeState_ = internAtom("WM_CHANGE_STATE");
    wmWindowType_ = internAtom("_NET_WM_WINDOW_TYPE");
    wmWindowTypeDesktop_ = internAtom("_NET_WM_WINDOW_TYPE_DESKTOP");
    wmWindowTypeDock_ = internAtom("_NET_WM_WINDOW_TYPE_DOCK");
    wmWindowTypeDialog_ = internAtom("_NET_WM_WINDOW_TYPE_DIALOG");
    wmNormalHints_ = internAtom("WM_NORMAL_HINTS");
    motifWmHints_ = internAtom("_MOTIF_WM_HINTS");
    gtkFrameExtents_ = internAtom("_GTK_FRAME_EXTENTS");
    netWmMoveResize_ = internAtom("_NET_WM_MOVERESIZE");

    if (screen_ != nullptr && screen_->root_depth >= 24) {
        // Frosted silver edges — matches dock/title glass, not the old green chrome.
        borderFocusedPixel_ = 0x00B0B6C0;
        borderUnfocusedPixel_ = 0x00D2D2D7;
    } else {
        borderFocusedPixel_ = screen_ != nullptr ? screen_->white_pixel : 0;
        borderUnfocusedPixel_ = screen_ != nullptr ? screen_->black_pixel : 0;
    }

    supportingWindow_ = xcb_generate_id(connection_);

    const auto supportingCookie = xcb_create_window_checked(
        connection_,
        0,
        supportingWindow_,
        screen_->root,
        -1,
        -1,
        1,
        1,
        0,
        XCB_WINDOW_CLASS_INPUT_ONLY,
        XCB_COPY_FROM_PARENT,
        0,
        nullptr);
    xcb_generic_error_t* supportingError =
        xcb_request_check(connection_, supportingCookie);

    if (supportingError != nullptr) {
        std::free(supportingError);
        supportingWindow_ = XCB_WINDOW_NONE;
    }

    publishRootProperties();

    for (const auto& [client, window] : windows_) {
        (void)client;
        publishClientState(window);
    }

    publishClientList();
    drawPanel();
    xcb_flush(connection_);

    std::cout
        << "Eden anunciou EWMH, "
        << kDesktopCount
        << " áreas de trabalho.\n";
}

void WindowManager::releaseManagedClients()
{
    if (connection_ == nullptr || screen_ == nullptr) {
        windows_.clear();
        return;
    }

    std::vector<xcb_window_t> clients;
    clients.reserve(windows_.size());

    for (const auto& [client, window] : windows_) {
        (void)window;
        clients.push_back(client);
    }

    for (const xcb_window_t client : clients) {
        unmanageWindow(client);
    }

    if (panel_ != XCB_WINDOW_NONE) {
        xcb_destroy_window(connection_, panel_);
        panel_ = XCB_WINDOW_NONE;
    }

    if (supportingWindow_ != XCB_WINDOW_NONE) {
        xcb_destroy_window(connection_, supportingWindow_);
        supportingWindow_ = XCB_WINDOW_NONE;
    }

    xcb_flush(connection_);
}

void WindowManager::clearProtocolState()
{
    wmProtocols_ = XCB_ATOM_NONE;
    wmDeleteWindow_ = XCB_ATOM_NONE;
    wmTakeFocus_ = XCB_ATOM_NONE;
    wmWindowType_ = XCB_ATOM_NONE;
    wmWindowTypeDesktop_ = XCB_ATOM_NONE;
    wmWindowTypeDock_ = XCB_ATOM_NONE;
    wmWindowTypeDialog_ = XCB_ATOM_NONE;
    wmNormalHints_ = XCB_ATOM_NONE;
    wmState_ = XCB_ATOM_NONE;
    wmChangeState_ = XCB_ATOM_NONE;
    netSupported_ = XCB_ATOM_NONE;
    netSupportingWmCheck_ = XCB_ATOM_NONE;
    netWmName_ = XCB_ATOM_NONE;
    netClientList_ = XCB_ATOM_NONE;
    netActiveWindow_ = XCB_ATOM_NONE;
    netWmState_ = XCB_ATOM_NONE;
    netWmStateMaximizedVert_ = XCB_ATOM_NONE;
    netWmStateMaximizedHorz_ = XCB_ATOM_NONE;
    netWmStateHidden_ = XCB_ATOM_NONE;
    netWmStateFullscreen_ = XCB_ATOM_NONE;
    netCloseWindow_ = XCB_ATOM_NONE;
    netCurrentDesktop_ = XCB_ATOM_NONE;
    netNumberOfDesktops_ = XCB_ATOM_NONE;
    netWmDesktop_ = XCB_ATOM_NONE;
    netFrameExtents_ = XCB_ATOM_NONE;
    netWorkarea_ = XCB_ATOM_NONE;
    netDesktopGeometry_ = XCB_ATOM_NONE;
    netDesktopViewport_ = XCB_ATOM_NONE;
    netWmStrut_ = XCB_ATOM_NONE;
    netWmStrutPartial_ = XCB_ATOM_NONE;
    netWmMoveResize_ = XCB_ATOM_NONE;
    utf8String_ = XCB_ATOM_NONE;
    panel_ = XCB_WINDOW_NONE;
    supportingWindow_ = XCB_WINDOW_NONE;
    desktopWindows_.clear();
    focusOrder_.clear();
    panelClients_.clear();
    currentDesktop_ = 0;
    reservedLeft_ = 0;
    reservedRight_ = 0;
    reservedTop_ = 0;
    reservedBottom_ = 0;
}

void WindowManager::processClientMessage(xcb_generic_event_t* event)
{
    const auto* message =
        reinterpret_cast<const xcb_client_message_event_t*>(event);

    if (message->type == netCurrentDesktop_) {
        switchDesktop(message->data.data32[0]);
        return;
    }

    auto iterator = windows_.find(message->window);

    if (iterator == windows_.end()) {
        return;
    }

    ManagedWindow& window = iterator->second;

    if (message->type == netWmMoveResize_) {
        beginNetWmMoveResize(
            window,
            static_cast<int16_t>(message->data.data32[0]),
            static_cast<int16_t>(message->data.data32[1]),
            message->data.data32[2]);
        return;
    }

    if (message->type == netActiveWindow_) {
        if (!window.sticky && window.desktop != currentDesktop_) {
            switchDesktop(window.desktop);
        }

        focusWindow(window, XCB_CURRENT_TIME);
        return;
    }

    if (message->type == netCloseWindow_) {
        closeWindow(window);
        return;
    }

    if (message->type == wmChangeState_ &&
        message->data.data32[0] == kIconicState) {
        minimizeWindow(window);
        return;
    }

    if (message->type == netWmDesktop_) {
        const uint32_t desktop = message->data.data32[0];

        if (desktop == kAllDesktops) {
            window.sticky = true;
        } else if (desktop < kDesktopCount) {
            window.sticky = false;
            window.desktop = desktop;
        }

        publishClientState(window);

        if (window.minimized) {
            drawPanel();
            return;
        }

        if (window.sticky || window.desktop == currentDesktop_) {
            xcb_map_window(connection_, window.frame);
        } else {
            xcb_unmap_window(connection_, window.frame);

            if (focusedFrame_ == window.frame) {
                clearFocus();
                focusAnotherVisible(window.client);
            }
        }

        drawPanel();
        xcb_flush(connection_);
        return;
    }

    if (message->type != netWmState_) {
        return;
    }

    const uint32_t action = message->data.data32[0];
    const xcb_atom_t first = message->data.data32[1];
    const xcb_atom_t second = message->data.data32[2];
    const auto isMaximize = [&](xcb_atom_t atom) {
        return atom == netWmStateMaximizedVert_ ||
               atom == netWmStateMaximizedHorz_ ||
               atom == netWmStateFullscreen_;
    };

    if (isMaximize(first) || isMaximize(second)) {
        bool want = window.maximized;

        if (action == kNetWmStateAdd) {
            want = true;
        } else if (action == kNetWmStateRemove) {
            want = false;
        } else if (action == kNetWmStateToggle) {
            want = !window.maximized;
        }

        if (want != window.maximized) {
            toggleMaximize(window);
        }
    }

    applyNetWmState(window, action, first);

    if (second != first) {
        applyNetWmState(window, action, second);
    }
    publishClientState(window);
    xcb_flush(connection_);
}
