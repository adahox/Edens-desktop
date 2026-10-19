#include "windowmanager.hpp"
#include "titlebarlayout.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

void WindowManager::processMessages(xcb_generic_event_t* event)
{
    if (event == nullptr ||
        connection_ == nullptr ||
        xcb_connection_has_error(connection_) != 0) {
        stop();
        return;
    }

    const uint8_t responseType = event->response_type & 0x7f;

    switch (responseType) {
    case XCB_KEY_PRESS:
        processKeyPress(event);
        break;
    case XCB_BUTTON_PRESS:
        processButtonPress(event);
        break;
    case XCB_BUTTON_RELEASE:
        processButtonRelease();
        break;
    case XCB_MOTION_NOTIFY:
        processPointerMotion(event);
        break;
    case XCB_MAP_REQUEST:
        processMapRequest(event);
        break;
    case XCB_MAP_NOTIFY:
        processMapNotify(event);
        break;
    case XCB_CONFIGURE_NOTIFY:
        processConfigureNotify(event);
        break;
    case XCB_DESTROY_NOTIFY:
        processDestroyNotify(event);
        break;
    case XCB_CONFIGURE_REQUEST:
        processConfigureRequest(event);
        break;
    case XCB_EXPOSE:
        processExpose(event);
        break;
    case XCB_UNMAP_NOTIFY:
        processUnmapNotify(event);
        break;
    case XCB_CLIENT_MESSAGE:
        processClientMessage(event);
        break;
    default:
        break;
    }
}

namespace {

std::string siblingExecutable(const char* name)
{
    char path[4096];
    const ssize_t length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);

    if (length <= 0) {
        return name;
    }

    path[length] = '\0';
    const std::string executable(path);
    const auto slash = executable.find_last_of('/');

    if (slash == std::string::npos) {
        return name;
    }

    return executable.substr(0, slash + 1) + name;
}

void launchTerminal()
{
    const pid_t process = fork();

    if (process != 0) {
        return;
    }

    setsid();
    execlp("x-terminal-emulator", "x-terminal-emulator", nullptr);
    execlp("gnome-terminal", "gnome-terminal", nullptr);
    execlp("xfce4-terminal", "xfce4-terminal", nullptr);
    execlp("xterm", "xterm", nullptr);
    _exit(1);
}

}  // namespace

void WindowManager::processKeyPress(xcb_generic_event_t* event)
{
    const auto* key = reinterpret_cast<xcb_key_press_event_t*>(event);
    const bool super = (key->state & XCB_MOD_MASK_4) != 0;
    const bool alt = (key->state & XCB_MOD_MASK_1) != 0;
    const bool control = (key->state & XCB_MOD_MASK_CONTROL) != 0;
    const bool shift = (key->state & XCB_MOD_MASK_SHIFT) != 0;

    if (alt && key->detail == 23) {
        cycleFocus(shift ? -1 : 1);
        return;
    }

    if (control && alt && key->detail == 113) {
        switchDesktop(currentDesktop_ == 0 ? kDesktopCount - 1 : currentDesktop_ - 1);
        return;
    }

    if (control && alt && key->detail == 114) {
        switchDesktop((currentDesktop_ + 1) % kDesktopCount);
        return;
    }

    if (super && key->detail == 58) {
        if (ManagedWindow* window = findByFrame(focusedFrame_)) {
            if (window->minimized) {
                restoreWindow(*window);
            } else {
                minimizeWindow(*window);
            }
        }

        return;
    }

    if (super && key->detail == 9) {
        stop();
        return;
    }

    if (super && key->detail == 36) {
        launchTerminal();
        return;
    }

    if (super && key->detail == 26) {
        const pid_t process = fork();

        if (process != 0) {
            return;
        }

        setsid();
        const std::string edens = siblingExecutable("edens");
        execl(edens.c_str(), edens.c_str(), nullptr);
        execlp("edens", "edens", nullptr);
        _exit(1);
    }

    if ((super && key->detail == 24) || (alt && key->detail == 70)) {
        if (focusedFrame_ == XCB_WINDOW_NONE) {
            return;
        }

        if (ManagedWindow* window = findByFrame(focusedFrame_)) {
            closeWindow(*window);
        }
    }
}

void WindowManager::processUnmapNotify(xcb_generic_event_t* event)
{
    const auto* unmapEvent =
        reinterpret_cast<xcb_unmap_notify_event_t*>(event);

    const auto iterator = windows_.find(unmapEvent->window);

    if (iterator == windows_.end() || iterator->second.minimized) {
        return;
    }

    unmanageWindow(iterator->first);
}

void WindowManager::processButtonPress(xcb_generic_event_t* event)
{
    const auto* button =
        reinterpret_cast<xcb_button_press_event_t*>(event);

    if (button->event == panel_) {
        handlePanelPress(button->event_x);
        return;
    }

    auto client = windows_.find(button->event);

    if (client == windows_.end() && button->child != XCB_WINDOW_NONE) {
        client = windows_.find(button->child);
    }

    if (client != windows_.end()) {
        focusWindow(client->second, button->time);
        xcb_allow_events(
            connection_,
            XCB_ALLOW_REPLAY_POINTER,
            button->time);
        xcb_flush(connection_);
        return;
    }

    // --- CLIQUE NA MOLDURA / BORDA (RESIZE) ---
    if (ManagedWindow* frameWindow = findByFrame(button->event)) {
        focusWindow(*frameWindow, button->time);

        if (button->detail != XCB_BUTTON_INDEX_1) {
            return;
        }

        const auto frameCookie = xcb_get_geometry(connection_, frameWindow->frame);
        xcb_generic_error_t* frameError = nullptr;
        xcb_get_geometry_reply_t* frameGeometry =
            xcb_get_geometry_reply(connection_, frameCookie, &frameError);

        std::free(frameError);

        if (frameGeometry == nullptr) {
            return;
        }

        const ResizeDirection dir = resizeDirectionAt(
            frameGeometry->width,
            frameGeometry->height,
            button->event_x,
            button->event_y
        );

        if (dir != ResizeDirection::None) {
            resizingFrame_ = frameWindow->frame;
            resizingDirection_ = dir;
            dragStartX_ = button->root_x;
            dragStartY_ = button->root_y;
            frameStartX_ = frameGeometry->x;
            frameStartY_ = frameGeometry->y;
            frameStartWidth_ = frameGeometry->width;
            frameStartHeight_ = frameGeometry->height;

            xcb_grab_pointer(
                connection_,
                0,
                frameWindow->frame,
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC,
                XCB_WINDOW_NONE,
                XCB_CURSOR_NONE,
                XCB_CURRENT_TIME
            );
        }

        std::free(frameGeometry);
        xcb_flush(connection_);
        return;
    }

    // --- CLIQUE NA BARRA DE TÍTULO ---
    ManagedWindow* window = findByTitleBar(button->event);

    if (window == nullptr) {
        return;
    }

    focusWindow(*window, button->time);
    processTitleBarPress(*window, button);
}

void WindowManager::processTitleBarPress(
    ManagedWindow& window,
    const xcb_button_press_event_t* button)
{
    if (button->detail != XCB_BUTTON_INDEX_1) {
        return;
    }

    const auto barCookie = xcb_get_geometry(connection_, window.titleBar);

    xcb_generic_error_t* barError = nullptr;
    xcb_get_geometry_reply_t* barGeometry =
        xcb_get_geometry_reply(connection_, barCookie, &barError);

    std::free(barError);

    if (barGeometry == nullptr) {
        return;
    }

    const TitleButton clicked = titleButtonAt(
        barGeometry->width,
        button->event_x,
        button->event_y);

    std::free(barGeometry);

    switch (clicked) {
    case TitleButton::Minimize:
        minimizeWindow(window);
        return;
    case TitleButton::Maximize:
        toggleMaximize(window);
        return;
    case TitleButton::Close:
        closeWindow(window);
        return;
    case TitleButton::None:
        break;
    }

    const auto frameCookie = xcb_get_geometry(connection_, window.frame);

    xcb_generic_error_t* frameError = nullptr;
    xcb_get_geometry_reply_t* frameGeometry =
        xcb_get_geometry_reply(connection_, frameCookie, &frameError);

    std::free(frameError);

    if (frameGeometry == nullptr) {
        return;
    }

    draggingFrame_ = window.frame;
    dragStartX_ = button->root_x;
    dragStartY_ = button->root_y;
    frameStartX_ = frameGeometry->x;
    frameStartY_ = frameGeometry->y;
    std::free(frameGeometry);

    xcb_grab_pointer(
        connection_,
        0,
        window.titleBar,
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC,
        XCB_WINDOW_NONE,
        XCB_CURSOR_NONE,
        XCB_CURRENT_TIME);

    xcb_flush(connection_);
}

void WindowManager::processPointerMotion(xcb_generic_event_t* event)
{
    const auto* motion = reinterpret_cast<xcb_motion_notify_event_t*>(event);

    if (resizingFrame_ != XCB_WINDOW_NONE) {
        processResizeMotion(motion);
        return;
    }

    if (draggingFrame_ == XCB_WINDOW_NONE) {
        return;
    }

    const int16_t newX = static_cast<int16_t>(frameStartX_ + (motion->root_x - dragStartX_));
    const int16_t newY = static_cast<int16_t>(frameStartY_ + (motion->root_y - dragStartY_));

    const uint32_t values[] = {
        packedCoordinate(newX),
        packedCoordinate(newY)
    };

    xcb_configure_window(
        connection_,
        draggingFrame_,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
        values
    );

    xcb_flush(connection_);
}

void WindowManager::processButtonRelease()
{
    draggingFrame_ = XCB_WINDOW_NONE;
    resizingFrame_ = XCB_WINDOW_NONE;
    resizingDirection_ = ResizeDirection::None;

    xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
    xcb_flush(connection_);
}

void WindowManager::processMapRequest(xcb_generic_event_t* event)
{
    const auto* mapEvent =
        reinterpret_cast<xcb_map_request_event_t*>(event);

    if (isDockWindow(mapEvent->window)) {
        noteDock(mapEvent->window);
        return;
    }

    if (isDesktopWindow(mapEvent->window)) {
        desktopWindows_.insert(mapEvent->window);
        xcb_map_window(connection_, mapEvent->window);
        fitDesktopWindow(mapEvent->window);

        const uint32_t stackMode[] = {XCB_STACK_MODE_BELOW};
        xcb_configure_window(
            connection_,
            mapEvent->window,
            XCB_CONFIG_WINDOW_STACK_MODE,
            stackMode);
        raisePanel();
        xcb_flush(connection_);
        return;
    }

    if (!manageWindow(mapEvent->window)) {
        xcb_map_window(connection_, mapEvent->window);
    }

    raisePanel();
    xcb_flush(connection_);
}

void WindowManager::processMapNotify(xcb_generic_event_t* event)
{
    const auto* mapEvent =
        reinterpret_cast<const xcb_map_notify_event_t*>(event);

    if (mapEvent->window == panel_ || mapEvent->window == supportingWindow_) {
        return;
    }

    if (windows_.contains(mapEvent->window)) {
        raisePanel();
        xcb_flush(connection_);
        return;
    }

    if (desktopWindows_.contains(mapEvent->window) || isDesktopWindow(mapEvent->window)) {
        desktopWindows_.insert(mapEvent->window);
        fitDesktopWindow(mapEvent->window);
    }

    raisePanel();
    xcb_flush(connection_);
}

void WindowManager::processConfigureNotify(xcb_generic_event_t* event)
{
    const auto* configureEvent =
        reinterpret_cast<const xcb_configure_notify_event_t*>(event);

    if (configureEvent->window == panel_ ||
        configureEvent->window == supportingWindow_) {
        return;
    }

    if (windows_.contains(configureEvent->window) ||
        findByFrame(configureEvent->window) != nullptr ||
        findByTitleBar(configureEvent->window) != nullptr) {
        raisePanel();
        return;
    }

    if (desktopWindows_.contains(configureEvent->window) ||
        isDesktopWindow(configureEvent->window)) {
        desktopWindows_.insert(configureEvent->window);
        fitDesktopWindow(configureEvent->window);
        return;
    }

    raisePanel();
}

void WindowManager::fitDesktopWindow(xcb_window_t window)
{
    if (!isConnected() || window == panel_) {
        return;
    }

    const MonitorRect area = workArea();
    const auto cookie = xcb_get_geometry(connection_, window);
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, cookie, nullptr);

    const bool alreadyFits =
        geometry != nullptr &&
        geometry->x == area.x &&
        geometry->y == area.y &&
        geometry->width == area.width &&
        geometry->height == area.height;

    std::free(geometry);

    if (!alreadyFits) {
        const uint32_t values[] = {
            packedCoordinate(area.x),
            packedCoordinate(area.y),
            area.width,
            area.height,
            XCB_STACK_MODE_BELOW
        };

        xcb_configure_window(
            connection_,
            window,
            XCB_CONFIG_WINDOW_X |
                XCB_CONFIG_WINDOW_Y |
                XCB_CONFIG_WINDOW_WIDTH |
                XCB_CONFIG_WINDOW_HEIGHT |
                XCB_CONFIG_WINDOW_STACK_MODE,
            values);
    }

    raisePanel();
}

void WindowManager::processDestroyNotify(xcb_generic_event_t* event)
{
    const auto* destroyEvent =
        reinterpret_cast<xcb_destroy_notify_event_t*>(event);

    desktopWindows_.erase(destroyEvent->window);

    const auto iterator = windows_.find(destroyEvent->window);

    if (iterator != windows_.end()) {
        const xcb_window_t client = iterator->first;
        const xcb_window_t frame = iterator->second.frame;

        if (draggingFrame_ == frame || resizingFrame_ == frame) {
            draggingFrame_ = XCB_WINDOW_NONE;
            resizingFrame_ = XCB_WINDOW_NONE;
            resizingDirection_ = ResizeDirection::None;
            xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
        }

        xcb_destroy_window(connection_, frame);
        windows_.erase(iterator);
        desktopWindows_.erase(client);
        desktopWindows_.erase(frame);
        noteWindowRemoved(client, frame);
        xcb_flush(connection_);
    }

    std::cout
        << "Janela destruída: "
        << destroyEvent->window << '\n';
}

void WindowManager::processConfigureRequest(xcb_generic_event_t* event)
{
    const auto* configureEvent =
        reinterpret_cast<xcb_configure_request_event_t*>(event);

    const auto managed = windows_.find(configureEvent->window);

    if (managed == windows_.end() && isDesktopWindow(configureEvent->window)) {
        desktopWindows_.insert(configureEvent->window);
        fitDesktopWindow(configureEvent->window);
        xcb_flush(connection_);
        return;
    }

    if (managed != windows_.end()) {
        if (managed->second.maximized) {
            return;
        }

        ManagedWindow& window = managed->second;
        const auto frameCookie = xcb_get_geometry(connection_, window.frame);
        xcb_generic_error_t* frameError = nullptr;
        xcb_get_geometry_reply_t* frameGeometry =
            xcb_get_geometry_reply(connection_, frameCookie, &frameError);

        std::free(frameError);

        if (frameGeometry == nullptr) {
            return;
        }

        int16_t x = frameGeometry->x;
        int16_t y = frameGeometry->y;
        uint16_t width = frameGeometry->width;
        uint16_t height = frameGeometry->height;
        const int16_t currentX = frameGeometry->x;
        const int16_t currentY = frameGeometry->y;
        const uint16_t currentWidth = frameGeometry->width;
        const uint16_t currentHeight = frameGeometry->height;
        std::free(frameGeometry);

        if ((configureEvent->value_mask & XCB_CONFIG_WINDOW_X) != 0) {
            x = configureEvent->x;
        }

        if ((configureEvent->value_mask & XCB_CONFIG_WINDOW_Y) != 0) {
            y = static_cast<int16_t>(configureEvent->y - kTitleBarHeight);
        }

        if ((configureEvent->value_mask & XCB_CONFIG_WINDOW_WIDTH) != 0) {
            width = configureEvent->width;
        }

        if ((configureEvent->value_mask & XCB_CONFIG_WINDOW_HEIGHT) != 0) {
            const uint32_t clientHeight = configureEvent->height;
            height = clientHeight + kTitleBarHeight > 65535u
                ? static_cast<uint16_t>(65535u)
                : static_cast<uint16_t>(clientHeight + kTitleBarHeight);
        }

        const FrameSize clamped = clampFrameSize(
            width,
            height,
            window.minimumWidth,
            window.minimumHeight,
            window.maximumWidth,
            window.maximumHeight,
            true);

        if (x == currentX &&
            y == currentY &&
            clamped.width == currentWidth &&
            clamped.height == currentHeight &&
            (configureEvent->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) == 0) {
            return;
        }

        layoutFrame(window, x, y, width, height, true);

        if ((configureEvent->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) != 0) {
            const uint32_t stackMode[] = {configureEvent->stack_mode};
            xcb_configure_window(
                connection_,
                window.frame,
                XCB_CONFIG_WINDOW_STACK_MODE,
                stackMode);
            raisePanel();
        }

        xcb_flush(connection_);
        return;
    }

    const uint16_t valueMask = configureEvent->value_mask;
    uint32_t values[] = {
        static_cast<uint32_t>(configureEvent->x),
        static_cast<uint32_t>(configureEvent->y),
        configureEvent->width,
        configureEvent->height,
        configureEvent->border_width,
        configureEvent->sibling,
        configureEvent->stack_mode
    };

    xcb_configure_window(
        connection_,
        configureEvent->window,
        valueMask,
        values);

    xcb_flush(connection_);

    std::cout
        << "Solicitação de configuração para janela: "
        << configureEvent->window << '\n';
}

void WindowManager::processExpose(xcb_generic_event_t* event)
{
    const auto* exposeEvent =
        reinterpret_cast<xcb_expose_event_t*>(event);

    if (exposeEvent->count != 0) {
        return;
    }

    if (exposeEvent->window == panel_) {
        drawPanel();
        return;
    }

    if (findByTitleBar(exposeEvent->window) != nullptr) {
        drawTitleBar(exposeEvent->window);
    }
}

ResizeDirection WindowManager::resizeDirectionAt(
    uint16_t frameWidth,
    uint16_t frameHeight,
    int16_t x,
    int16_t y)
{
    int dir = static_cast<int>(ResizeDirection::None);

    if (x >= 0 && x <= kResizeMargin) {
        dir |= static_cast<int>(ResizeDirection::Left);
    } else if (x >= static_cast<int16_t>(frameWidth - kResizeMargin) && x <= frameWidth) {
        dir |= static_cast<int>(ResizeDirection::Right);
    }

    if (y >= 0 && y <= kResizeMargin) {
        dir |= static_cast<int>(ResizeDirection::Top);
    } else if (y >= static_cast<int16_t>(frameHeight - kResizeMargin) && y <= frameHeight) {
        dir |= static_cast<int>(ResizeDirection::Bottom);
    }

    return static_cast<ResizeDirection>(dir);
}

void WindowManager::processResizeMotion(const xcb_motion_notify_event_t* motion)
{
    if (resizingFrame_ == XCB_WINDOW_NONE || resizingDirection_ == ResizeDirection::None) {
        return;
    }

    ManagedWindow* window = findByFrame(resizingFrame_);
    if (window == nullptr) {
        return;
    }

    const int16_t deltaX = motion->root_x - dragStartX_;
    const int16_t deltaY = motion->root_y - dragStartY_;

    int16_t newX = frameStartX_;
    int16_t newY = frameStartY_;
    int32_t newWidth = frameStartWidth_;
    int32_t newHeight = frameStartHeight_;

    const int dir = static_cast<int>(resizingDirection_);

    if (dir & static_cast<int>(ResizeDirection::Right)) {
        newWidth = frameStartWidth_ + deltaX;
    } else if (dir & static_cast<int>(ResizeDirection::Left)) {
        newWidth = frameStartWidth_ - deltaX;
        newX = frameStartX_ + deltaX;
    }

    if (dir & static_cast<int>(ResizeDirection::Bottom)) {
        newHeight = frameStartHeight_ + deltaY;
    } else if (dir & static_cast<int>(ResizeDirection::Top)) {
        newHeight = frameStartHeight_ - deltaY;
        newY = frameStartY_ + deltaY;
    }

    if (newWidth < window->minimumWidth) {
        if ((dir & static_cast<int>(ResizeDirection::Left)) != 0) {
            newX = static_cast<int16_t>(
                frameStartX_ + frameStartWidth_ - window->minimumWidth);
        }

        newWidth = window->minimumWidth;
    }

    if (newHeight < window->minimumHeight) {
        if ((dir & static_cast<int>(ResizeDirection::Top)) != 0) {
            newY = static_cast<int16_t>(
                frameStartY_ + frameStartHeight_ - window->minimumHeight);
        }

        newHeight = window->minimumHeight;
    }

    layoutFrame(*window, newX, newY, static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight));
    xcb_flush(connection_);
}
