#pragma once

#include "windowmanager/titlebarlayout.hpp"
#include "windowmanager/windowmanagerpolicy.hpp"

#include <xcb/xcb.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class WindowManager final
{
public:
    WindowManager() = default;
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    bool connect();
    bool acquireControl();
    void run();
    void stop();
    void disconnect();

    [[nodiscard]] bool isConnected() const noexcept;

private:
    struct ManagedWindow
    {
        xcb_window_t client = XCB_WINDOW_NONE;
        xcb_window_t frame = XCB_WINDOW_NONE;
        xcb_window_t titleBar = XCB_WINDOW_NONE;
        xcb_window_t transientFor = XCB_WINDOW_NONE;

        bool maximized = false;
        bool minimized = false;
        bool sticky = false;
        bool decorated = true;

        int16_t restoreX = 0;
        int16_t restoreY = 0;
        uint16_t restoreWidth = 0;
        uint16_t restoreHeight = 0;

        uint16_t minimumWidth = 150;
        uint16_t minimumHeight = 100;
        uint16_t maximumWidth = 0;
        uint16_t maximumHeight = 0;

        uint32_t desktop = 0;
        bool acceptsInput = true;
        int ignoreUnmap = 0;
        std::string title;
    };

    static constexpr uint16_t kPanelHeight = 32;
    static constexpr uint32_t kDesktopCount = 4;

    bool manageWindow(xcb_window_t client);
    void unmanageWindow(xcb_window_t client);
    void processMessages(xcb_generic_event_t* event);
    void processKeyPress(xcb_generic_event_t* event);
    void processKeyRelease(xcb_generic_event_t* event);
    void requestToggleSearch();
    void processButtonPress(xcb_generic_event_t* event);
    void processTitleBarPress(
        ManagedWindow& window,
        const xcb_button_press_event_t* button);
    void processPointerMotion(xcb_generic_event_t* event);
    void processTitleBarMotion(const xcb_motion_notify_event_t* motion);
    void processTitleBarLeave(xcb_window_t titleBar);
    void processButtonRelease();
    void clearTitleBarHover();
    void processUnmapNotify(xcb_generic_event_t* event);
    void processMapRequest(xcb_generic_event_t* event);
    void processMapNotify(xcb_generic_event_t* event);
    void processConfigureNotify(xcb_generic_event_t* event);
    void fitDesktopWindow(xcb_window_t window);
    void processDestroyNotify(xcb_generic_event_t* event);
    void processConfigureRequest(xcb_generic_event_t* event);
    void processClientMessage(xcb_generic_event_t* event);
    void beginNetWmMoveResize(
        ManagedWindow& window,
        int16_t rootX,
        int16_t rootY,
        uint32_t direction);
    bool grabPointerForOperation();
    void processExpose(xcb_generic_event_t* event);
    void drawTitleBar(xcb_window_t titleBar);
    ManagedWindow* findByTitleBar(xcb_window_t titleBar);
    ManagedWindow* findByFrame(xcb_window_t frame);
    void closeWindow(ManagedWindow& window);
    void minimizeWindow(ManagedWindow& window);
    void restoreWindow(ManagedWindow& window);
    void toggleMaximize(ManagedWindow& window);
    void layoutFrame(
        ManagedWindow& window,
        int16_t x,
        int16_t y,
        uint16_t width,
        uint16_t height,
        bool honorMaximum = true);
    void tightWrapFrame(ManagedWindow& window);
    void focusWindow(ManagedWindow& window, xcb_timestamp_t time);
    void clearFocus();
    void cycleFocus(int direction);
    void focusAnotherVisible(xcb_window_t exceptClient);
    void switchDesktop(uint32_t desktop);
    void ensureAtoms();
    bool clientSupportsProtocol(xcb_window_t client, xcb_atom_t protocol);
    bool clientAcceptsClose(xcb_window_t client);
    bool isDesktopWindow(xcb_window_t window);
    bool isDockWindow(xcb_window_t window);
    bool hasWindowType(xcb_window_t window, xcb_atom_t type);
    void noteDock(xcb_window_t window);
    void processResizeMotion(const xcb_motion_notify_event_t* motion);
    ResizeDirection resizeDirectionAt(
        uint16_t frameWidth,
        uint16_t frameHeight,
        int16_t x,
        int16_t y);

    void setupDesktopIntegration();
    void releaseManagedClients();
    void clearProtocolState();
    xcb_atom_t internAtom(const char* name);
    void readWindowHints(ManagedWindow& window);
    [[nodiscard]] bool clientWantsTitleBar(xcb_window_t client) const;
    void applyDecorationMode(ManagedWindow& window, bool decorated);
    void updateFrameBorder(const ManagedWindow& window, bool focused) const;
    void processPropertyNotify(xcb_generic_event_t* event);
    void placeTransient(ManagedWindow& window);
    void publishRootProperties();
    void publishClientList();
    void publishClientState(const ManagedWindow& window);
    void rememberFocus(xcb_window_t client);
    void forgetFocus(xcb_window_t client);
    void noteWindowRemoved(xcb_window_t client, xcb_window_t frame);
    void raiseTransients(xcb_window_t client);
    void raisePanel();
    void drawPanel();
    void handlePanelPress(int16_t x);
    void applyNetWmState(ManagedWindow& window, uint32_t action, xcb_atom_t atom);
    [[nodiscard]] MonitorRect workArea() const;
    [[nodiscard]] MonitorRect virtualDesktopBounds() const;
    [[nodiscard]] std::vector<MonitorRect> connectedMonitors() const;
    [[nodiscard]] MonitorRect workAreaAt(int16_t x, int16_t y) const;
    void refreshScreenLayoutIfNeeded();
    void refitAfterScreenChange();
    void revealManagedFrames();
    void sendConfigureNotify(
        const ManagedWindow& window,
        int16_t frameX,
        int16_t frameY,
        uint16_t frameWidth,
        uint16_t frameHeight);

    xcb_connection_t* connection_ = nullptr;
    xcb_screen_t* screen_ = nullptr;
    xcb_cursor_t pointerCursor_ = XCB_NONE;
    std::unordered_map<xcb_window_t, ManagedWindow> windows_;
    std::vector<xcb_window_t> focusOrder_;
    std::vector<xcb_window_t> panelClients_;

    xcb_window_t focusedFrame_ = XCB_WINDOW_NONE;
    xcb_window_t draggingFrame_ = XCB_WINDOW_NONE;
    xcb_window_t panel_ = XCB_WINDOW_NONE;
    std::unordered_set<xcb_window_t> desktopWindows_;
    xcb_window_t supportingWindow_ = XCB_WINDOW_NONE;
    xcb_window_t hoveredTitleBar_ = XCB_WINDOW_NONE;
    TitleButton hoveredButton_ = TitleButton::None;
    xcb_window_t lastTitleClickBar_ = XCB_WINDOW_NONE;
    xcb_timestamp_t lastTitleClickTime_ = 0;
    int16_t lastTitleClickX_ = 0;
    int16_t lastTitleClickY_ = 0;

    int16_t dragStartX_ = 0;
    int16_t dragStartY_ = 0;
    xcb_timestamp_t lastPointerTime_ = XCB_CURRENT_TIME;
    int16_t frameStartX_ = 0;
    int16_t frameStartY_ = 0;

    ResizeDirection resizingDirection_ = ResizeDirection::None;
    xcb_window_t resizingFrame_ = XCB_WINDOW_NONE;
    int16_t resizeStartX_ = 0;
    int16_t resizeStartY_ = 0;
    int16_t frameStartWidth_ = 0;
    int16_t frameStartHeight_ = 0;

    uint32_t currentDesktop_ = 0;
    bool superHeld_ = false;
    bool superChord_ = false;
    bool keyboardGrabbed_ = false;
    uint16_t reservedLeft_ = 0;
    uint16_t reservedRight_ = 0;
    uint16_t reservedTop_ = 0;
    uint16_t reservedBottom_ = 0;
    MonitorRect cachedVirtualBounds_{0, 0, 0, 0};
    bool hasCachedVirtualBounds_ = false;

    xcb_atom_t wmProtocols_ = XCB_ATOM_NONE;
    xcb_atom_t wmDeleteWindow_ = XCB_ATOM_NONE;
    xcb_atom_t wmTakeFocus_ = XCB_ATOM_NONE;
    xcb_atom_t wmWindowType_ = XCB_ATOM_NONE;
    xcb_atom_t wmWindowTypeDesktop_ = XCB_ATOM_NONE;
    xcb_atom_t wmWindowTypeDock_ = XCB_ATOM_NONE;
    xcb_atom_t wmWindowTypeDialog_ = XCB_ATOM_NONE;
    xcb_atom_t wmNormalHints_ = XCB_ATOM_NONE;
    xcb_atom_t wmState_ = XCB_ATOM_NONE;
    xcb_atom_t wmChangeState_ = XCB_ATOM_NONE;
    xcb_atom_t netSupported_ = XCB_ATOM_NONE;
    xcb_atom_t netSupportingWmCheck_ = XCB_ATOM_NONE;
    xcb_atom_t netWmName_ = XCB_ATOM_NONE;
    xcb_atom_t netClientList_ = XCB_ATOM_NONE;
    xcb_atom_t netActiveWindow_ = XCB_ATOM_NONE;
    xcb_atom_t netWmState_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStateMaximizedVert_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStateMaximizedHorz_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStateHidden_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStateFullscreen_ = XCB_ATOM_NONE;
    xcb_atom_t netCloseWindow_ = XCB_ATOM_NONE;
    xcb_atom_t netCurrentDesktop_ = XCB_ATOM_NONE;
    xcb_atom_t netNumberOfDesktops_ = XCB_ATOM_NONE;
    xcb_atom_t netWmDesktop_ = XCB_ATOM_NONE;
    xcb_atom_t netFrameExtents_ = XCB_ATOM_NONE;
    xcb_atom_t netWorkarea_ = XCB_ATOM_NONE;
    xcb_atom_t netDesktopGeometry_ = XCB_ATOM_NONE;
    xcb_atom_t netDesktopViewport_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStrut_ = XCB_ATOM_NONE;
    xcb_atom_t netWmStrutPartial_ = XCB_ATOM_NONE;
    xcb_atom_t utf8String_ = XCB_ATOM_NONE;
    xcb_atom_t motifWmHints_ = XCB_ATOM_NONE;
    xcb_atom_t gtkFrameExtents_ = XCB_ATOM_NONE;
    xcb_atom_t netWmMoveResize_ = XCB_ATOM_NONE;
    uint32_t borderFocusedPixel_ = 0;
    uint32_t borderUnfocusedPixel_ = 0;

    bool controlAcquired_ = false;
    bool running_ = false;
};
