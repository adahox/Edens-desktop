#include "windowmanager.hpp"
#include "titlebarlayout.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>

namespace {

constexpr xcb_keycode_t kKeyReturn = 36;
constexpr xcb_keycode_t kKeyE = 26;
constexpr xcb_keycode_t kKeyQ = 24;
constexpr xcb_keycode_t kKeyF4 = 70;
constexpr xcb_keycode_t kKeyTab = 23;
constexpr xcb_keycode_t kKeyLeft = 113;
constexpr xcb_keycode_t kKeyRight = 114;
constexpr xcb_keycode_t kKeyM = 58;
constexpr xcb_keycode_t kKeyEscape = 9;

void grabHotkey(
    xcb_connection_t* connection,
    xcb_window_t root,
    uint16_t modifiers,
    xcb_keycode_t key)
{
    const uint16_t locks[] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)
    };

    for (const uint16_t lock : locks) {
        xcb_grab_key(
            connection,
            1,
            root,
            static_cast<uint16_t>(modifiers | lock),
            key,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC);
    }
}

}  // namespace

namespace {

uint32_t colorPixel(
    xcb_connection_t* connection,
    xcb_colormap_t colormap,
    uint16_t red,
    uint16_t green,
    uint16_t blue,
    uint32_t fallback)
{
    const auto cookie = xcb_alloc_color(
        connection,
        colormap,
        red,
        green,
        blue);

    xcb_alloc_color_reply_t* reply =
        xcb_alloc_color_reply(connection, cookie, nullptr);

    if (reply == nullptr) {
        return fallback;
    }

    const uint32_t pixel = reply->pixel;
    std::free(reply);
    return pixel;
}

void fillButton(
    xcb_connection_t* connection,
    xcb_window_t frame,
    xcb_gcontext_t gc,
    const TitleButtonLayout& button)
{
    const xcb_rectangle_t rectangle = {
        button.x,
        button.y,
        kButtonSize,
        kButtonSize
    };

    xcb_poly_fill_rectangle(connection, frame, gc, 1, &rectangle);
}

void drawMinimizeGlyph(
    xcb_connection_t* connection,
    xcb_window_t frame,
    xcb_gcontext_t gc,
    const TitleButtonLayout& button)
{
    const int16_t y = static_cast<int16_t>(button.y + (kButtonSize / 2));
    const xcb_segment_t segment = {
        static_cast<int16_t>(button.x + 5),
        y,
        static_cast<int16_t>(button.x + kButtonSize - 5),
        y
    };

    xcb_poly_segment(connection, frame, gc, 1, &segment);
}

void drawMaximizeGlyph(
    xcb_connection_t* connection,
    xcb_window_t frame,
    xcb_gcontext_t gc,
    const TitleButtonLayout& button,
    bool maximized)
{
    if (maximized) {
        const xcb_rectangle_t back = {
            static_cast<int16_t>(button.x + 7),
            static_cast<int16_t>(button.y + 4),
            8,
            8
        };
        const xcb_rectangle_t front = {
            static_cast<int16_t>(button.x + 4),
            static_cast<int16_t>(button.y + 7),
            8,
            8
        };

        xcb_poly_rectangle(connection, frame, gc, 1, &back);
        xcb_poly_rectangle(connection, frame, gc, 1, &front);
        return;
    }

    const xcb_rectangle_t rectangle = {
        static_cast<int16_t>(button.x + 4),
        static_cast<int16_t>(button.y + 4),
        static_cast<uint16_t>(kButtonSize - 8),
        static_cast<uint16_t>(kButtonSize - 8)
    };

    xcb_poly_rectangle(connection, frame, gc, 1, &rectangle);
}

void drawCloseGlyph(
    xcb_connection_t* connection,
    xcb_window_t frame,
    xcb_gcontext_t gc,
    const TitleButtonLayout& button)
{
    const int16_t left = static_cast<int16_t>(button.x + 5);
    const int16_t right = static_cast<int16_t>(button.x + kButtonSize - 5);
    const int16_t top = static_cast<int16_t>(button.y + 5);
    const int16_t bottom = static_cast<int16_t>(button.y + kButtonSize - 5);

    const xcb_segment_t segments[] = {
        {left, top, right, bottom},
        {right, top, left, bottom}
    };

    xcb_poly_segment(connection, frame, gc, 2, segments);
}

}  // namespace

WindowManager::~WindowManager()
{
    disconnect();
}

bool WindowManager::connect()
{
    if (isConnected()) {
        return true;
    }

    disconnect();

    int screenNumber = 0;
    connection_ = xcb_connect(nullptr, &screenNumber);

    if (connection_ == nullptr ||
        xcb_connection_has_error(connection_) != 0) {
        disconnect();
        return false;
    }

    const xcb_setup_t* setup = xcb_get_setup(connection_);

    if (setup == nullptr) {
        disconnect();
        return false;
    }

    xcb_screen_iterator_t iterator =
        xcb_setup_roots_iterator(setup);

    for (int index = 0;
         index < screenNumber && iterator.rem;
         ++index) {
        xcb_screen_next(&iterator);
    }

    if (!iterator.rem || iterator.data == nullptr) {
        disconnect();
        return false;
    }

    screen_ = iterator.data;

    return true;
}

bool WindowManager::acquireControl()
{
    if (!isConnected()) {
        return false;
    }

    if (controlAcquired_) {
        return true;
    }

    const uint32_t eventMask =
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    const auto cookie = xcb_change_window_attributes_checked(
        connection_,
        screen_->root,
        XCB_CW_EVENT_MASK,
        &eventMask
    );

    xcb_generic_error_t* error =
        xcb_request_check(connection_, cookie);

    if (error != nullptr) {
        std::free(error);
        return false;
    }

    controlAcquired_ = true;
    std::signal(SIGCHLD, SIG_IGN);

    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_4, kKeyReturn);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_4, kKeyE);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_4, kKeyQ);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_4, kKeyM);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_4, kKeyEscape);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_1, kKeyF4);
    grabHotkey(connection_, screen_->root, XCB_MOD_MASK_1, kKeyTab);
    grabHotkey(
        connection_,
        screen_->root,
        XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_1,
        kKeyTab);
    grabHotkey(
        connection_,
        screen_->root,
        XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1,
        kKeyLeft);
    grabHotkey(
        connection_,
        screen_->root,
        XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1,
        kKeyRight);

    // Escanear janelas já existentes na tela antes do Eden rodar
    const auto treeCookie = xcb_query_tree(connection_, screen_->root);
    xcb_query_tree_reply_t* tree = xcb_query_tree_reply(connection_, treeCookie, nullptr);

    if (tree != nullptr) {
        const int count = xcb_query_tree_children_length(tree);
        xcb_window_t* children = xcb_query_tree_children(tree);

        for (int i = 0; i < count; ++i) {
            const auto attrCookie = xcb_get_window_attributes(connection_, children[i]);
            xcb_get_window_attributes_reply_t* attr =
                xcb_get_window_attributes_reply(connection_, attrCookie, nullptr);

            if (attr != nullptr) {
                const bool ownWindow =
                    children[i] == panel_ || children[i] == supportingWindow_;

                if (!ownWindow && isDesktopWindow(children[i])) {
                    desktopWindows_.insert(children[i]);
                    fitDesktopWindow(children[i]);
                } else if (!ownWindow && isDockWindow(children[i])) {
                    noteDock(children[i]);
                } else if (!ownWindow &&
                           !attr->override_redirect &&
                           attr->map_state == XCB_MAP_STATE_VIEWABLE) {
                    manageWindow(children[i]);
                }

                std::free(attr);
            }
        }
        std::free(tree);
    }

    setupDesktopIntegration();
    return true;
}

void WindowManager::disconnect()
{
    if (connection_ != nullptr &&
        screen_ != nullptr &&
        xcb_connection_has_error(connection_) == 0) {
        releaseManagedClients();
    } else {
        windows_.clear();
    }

    controlAcquired_ = false;
    screen_ = nullptr;
    focusedFrame_ = XCB_WINDOW_NONE;
    draggingFrame_ = XCB_WINDOW_NONE;
    resizingFrame_ = XCB_WINDOW_NONE;
    resizingDirection_ = ResizeDirection::None;
    clearProtocolState();

    if (connection_ != nullptr) {
        xcb_disconnect(connection_);
        connection_ = nullptr;
    }
}

bool WindowManager::isConnected() const noexcept
{
    return connection_ != nullptr &&
           xcb_connection_has_error(connection_) == 0 &&
           screen_ != nullptr;
}

void WindowManager::run()
{
    if (!isConnected() || !controlAcquired_) {
        std::cerr
            << "O WindowManager precisa estar conectado "
            << "e ter adquirido o controle.\n";
        return;
    }

    running_ = true;

    std::cout << "Eden WindowManager ativo. Pressione Ctrl+C para sair.\n";

    while (running_) {
        xcb_generic_event_t* event =
            xcb_wait_for_event(connection_);

        if (event == nullptr) {
            if (xcb_connection_has_error(connection_) != 0) {
                std::cerr << "A conexão X11 foi encerrada.\n";
                break;
            }

            continue;
        }

        processMessages(event);
        std::free(event);
    }

    running_ = false;
}

void WindowManager::stop()
{
    running_ = false;
}


bool WindowManager::isDesktopWindow(xcb_window_t window)
{
    if (wmWindowTypeDesktop_ == XCB_ATOM_NONE) {
        wmWindowTypeDesktop_ = internAtom("_NET_WM_WINDOW_TYPE_DESKTOP");
    }

    return hasWindowType(window, wmWindowTypeDesktop_);
}

bool WindowManager::manageWindow(xcb_window_t client)
{
    if (!isConnected() ||
        windows_.contains(client) ||
        client == panel_ ||
        client == supportingWindow_) {
        return false;
    }

    const auto attributesCookie = xcb_get_window_attributes(connection_, client);
    xcb_generic_error_t* attributesError = nullptr;
    xcb_get_window_attributes_reply_t* attributes =
        xcb_get_window_attributes_reply(
            connection_,
            attributesCookie,
            &attributesError);

    std::free(attributesError);

    if (attributes == nullptr) {
        return false;
    }

    const bool overrideRedirect = attributes->override_redirect != 0;
    std::free(attributes);

    if (overrideRedirect || isDesktopWindow(client) || isDockWindow(client)) {
        return false;
    }

    const auto geometryCookie =
        xcb_get_geometry(connection_, client);

    xcb_generic_error_t* geometryError = nullptr;
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(
            connection_,
            geometryCookie,
            &geometryError
        );

    if (geometryError != nullptr || geometry == nullptr) {
        std::free(geometryError);
        std::free(geometry);
        return false;
    }

    const uint16_t width = geometry->width;
    const uint16_t height = geometry->height;
    const int16_t x = geometry->x;
    const int16_t y = geometry->y;

    std::free(geometry);

    const xcb_window_t frame = xcb_generate_id(connection_);
    const uint32_t frameValues[] = {
        screen_->black_pixel,
        XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION
    };

    xcb_create_window(
        connection_,
        XCB_COPY_FROM_PARENT,
        frame,
        screen_->root,
        x,
        y,
        width,
        height + kTitleBarHeight,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen_->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        frameValues
    );

    const xcb_window_t titleBar = xcb_generate_id(connection_);
    const uint32_t titleBarValues[] = {
        screen_->black_pixel,
        XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION
    };

    xcb_create_window(
        connection_,
        XCB_COPY_FROM_PARENT,
        titleBar,
        frame,
        0,
        0,
        width,
        kTitleBarHeight,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen_->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        titleBarValues
    );

    xcb_reparent_window(
        connection_,
        client,
        frame,
        0,
        kTitleBarHeight
    );

    const uint32_t clientEventMask =
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_BUTTON_PRESS;

    xcb_change_window_attributes(
        connection_,
        client,
        XCB_CW_EVENT_MASK,
        &clientEventMask
    );

    for (uint8_t button = XCB_BUTTON_INDEX_1; button <= XCB_BUTTON_INDEX_3; ++button) {
        xcb_grab_button(
            connection_,
            0,                  
            client,
            XCB_EVENT_MASK_BUTTON_PRESS,
            XCB_GRAB_MODE_SYNC,
            XCB_GRAB_MODE_ASYNC,
            XCB_WINDOW_NONE,
            XCB_CURSOR_NONE,
            button,
            XCB_MOD_MASK_ANY
        );
    }

    xcb_map_window(connection_, titleBar);
    xcb_map_window(connection_, client);
    xcb_map_window(connection_, frame);

    ManagedWindow& managed =
        windows_.emplace(client, ManagedWindow{client, frame, titleBar})
            .first->second;

    managed.desktop = currentDesktop_;
    readWindowHints(managed);
    placeTransient(managed);

    if (managed.sticky || managed.desktop == currentDesktop_) {
        focusWindow(managed, XCB_CURRENT_TIME);
    } else {
        publishClientState(managed);
        publishClientList();
        drawPanel();
    }

    xcb_flush(connection_);

    std::cout
        << "Janela decorada: " << client
        << " (moldura " << frame
        << ", barra " << titleBar << ")\n";

    return true;
}

void WindowManager::unmanageWindow(xcb_window_t client)
{
    const auto iterator = windows_.find(client);

    if (iterator == windows_.end()) {
        return;
    }

    const xcb_window_t frame = iterator->second.frame;

    if (draggingFrame_ == frame || resizingFrame_ == frame) {
        draggingFrame_ = XCB_WINDOW_NONE;
        resizingFrame_ = XCB_WINDOW_NONE;
        resizingDirection_ = ResizeDirection::None;
        xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
    }

    int16_t x = 0;
    int16_t y = 0;
    const auto geometryCookie = xcb_get_geometry(connection_, frame);
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, geometryCookie, nullptr);

    if (geometry != nullptr) {
        x = geometry->x;
        y = static_cast<int16_t>(geometry->y + kTitleBarHeight);
        std::free(geometry);
    }

    const auto reparentCookie = xcb_reparent_window_checked(
        connection_,
        client,
        screen_->root,
        x,
        y);
    xcb_generic_error_t* reparentError = xcb_request_check(connection_, reparentCookie);
    std::free(reparentError);

    xcb_destroy_window(connection_, frame);
    windows_.erase(iterator);
    noteWindowRemoved(client, frame);
    xcb_flush(connection_);
}



void WindowManager::drawTitleBar(xcb_window_t titleBar)
{
    const auto geometryCookie = xcb_get_geometry(connection_, titleBar);

    xcb_generic_error_t* error = nullptr;
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, geometryCookie, &error);

    std::free(error);

    if (geometry == nullptr) {
        return;
    }

    const ManagedWindow* managed = findByTitleBar(titleBar);
    const bool maximized = managed != nullptr && managed->maximized;
    const bool focused =
        managed != nullptr && managed->frame == focusedFrame_;

    const uint32_t backgroundColor = focused
        ? colorPixel(
              connection_,
              screen_->default_colormap,
              0x2F2F,
              0x6F6F,
              0x4E4E,
              screen_->black_pixel)
        : colorPixel(
              connection_,
              screen_->default_colormap,
              0x2A2A,
              0x2A2A,
              0x2A2A,
              screen_->black_pixel);

    const uint32_t buttonColor = focused
        ? colorPixel(
              connection_,
              screen_->default_colormap,
              0x2458,
              0x5C5C,
              0x4040,
              backgroundColor)
        : colorPixel(
              connection_,
              screen_->default_colormap,
              0x3A3A,
              0x3A3A,
              0x3A3A,
              backgroundColor);

    const uint32_t closeColor = focused
        ? colorPixel(
              connection_,
              screen_->default_colormap,
              0xCCCC,
              0x3333,
              0x3333,
              buttonColor)
        : colorPixel(
              connection_,
              screen_->default_colormap,
              0x6A6A,
              0x3A3A,
              0x3A3A,
              buttonColor);

    const uint32_t titleColor = focused
        ? screen_->white_pixel
        : colorPixel(
              connection_,
              screen_->default_colormap,
              0x8C8C,
              0x8C8C,
              0x8C8C,
              screen_->white_pixel);

    const xcb_gcontext_t gc = xcb_generate_id(connection_);

    const uint32_t values[] = {
        XCB_GX_COPY,
        backgroundColor,
        2,
        0
    };

    xcb_create_gc(
        connection_,
        gc,
        titleBar,
        XCB_GC_FUNCTION |
            XCB_GC_FOREGROUND |
            XCB_GC_LINE_WIDTH |
            XCB_GC_GRAPHICS_EXPOSURES,
        values
    );

    const xcb_rectangle_t rectangle = {
        0,
        0,
        geometry->width,
        geometry->height
    };

    xcb_poly_fill_rectangle(
        connection_,
        titleBar,
        gc,
        1,
        &rectangle
    );

    xcb_change_gc(
        connection_,
        gc,
        XCB_GC_FOREGROUND,
        &titleColor
    );

    std::string title = "Eden Window";

    if (managed != nullptr && !managed->title.empty()) {
        title = managed->title;
    }

    if (title.size() > 40) {
        title.resize(40);
    }

    xcb_image_text_8(
        connection_,
        static_cast<uint8_t>(title.size()),
        titleBar,
        gc,
        10,
        21,
        title.c_str()
    );

    const TitleBarButtons buttons = titleBarButtons(geometry->width);

    xcb_change_gc(connection_, gc, XCB_GC_FOREGROUND, &buttonColor);
    fillButton(connection_, titleBar, gc, buttons.minimize);
    fillButton(connection_, titleBar, gc, buttons.maximize);

    xcb_change_gc(connection_, gc, XCB_GC_FOREGROUND, &closeColor);
    fillButton(connection_, titleBar, gc, buttons.close);

    xcb_change_gc(connection_, gc, XCB_GC_FOREGROUND, &titleColor);
    drawMinimizeGlyph(connection_, titleBar, gc, buttons.minimize);
    drawMaximizeGlyph(
        connection_,
        titleBar,
        gc,
        buttons.maximize,
        maximized);
    drawCloseGlyph(connection_, titleBar, gc, buttons.close);

    xcb_free_gc(connection_, gc);
    std::free(geometry);
    xcb_flush(connection_);
}

WindowManager::ManagedWindow* WindowManager::findByFrame(xcb_window_t frame)
{
    for (auto& [client, managed] : windows_) {
        (void)client;

        if (managed.frame == frame) {
            return &managed;
        }
    }

    return nullptr;
}

WindowManager::ManagedWindow*
WindowManager::findByTitleBar(xcb_window_t titleBar)
{
    for (auto& [client, managed] : windows_) {
        (void)client;

        if (managed.titleBar == titleBar) {
            return &managed;
        }
    }

    return nullptr;
}

void WindowManager::ensureAtoms()
{
    if (connection_ == nullptr ||
        (wmProtocols_ != XCB_ATOM_NONE &&
         wmDeleteWindow_ != XCB_ATOM_NONE &&
         wmTakeFocus_ != XCB_ATOM_NONE)) {
        return;
    }

    const auto protocolsCookie = xcb_intern_atom(
        connection_,
        0,
        12,
        "WM_PROTOCOLS");
    const auto deleteCookie = xcb_intern_atom(
        connection_,
        0,
        16,
        "WM_DELETE_WINDOW");
    const auto takeFocusCookie = xcb_intern_atom(
        connection_,
        0,
        13,
        "WM_TAKE_FOCUS");

    xcb_intern_atom_reply_t* protocols =
        xcb_intern_atom_reply(connection_, protocolsCookie, nullptr);
    xcb_intern_atom_reply_t* deleteWindow =
        xcb_intern_atom_reply(connection_, deleteCookie, nullptr);
    xcb_intern_atom_reply_t* takeFocus =
        xcb_intern_atom_reply(connection_, takeFocusCookie, nullptr);

    if (protocols != nullptr) {
        wmProtocols_ = protocols->atom;
        std::free(protocols);
    }

    if (deleteWindow != nullptr) {
        wmDeleteWindow_ = deleteWindow->atom;
        std::free(deleteWindow);
    }

    if (takeFocus != nullptr) {
        wmTakeFocus_ = takeFocus->atom;
        std::free(takeFocus);
    }
}

bool WindowManager::clientSupportsProtocol(
    xcb_window_t client,
    xcb_atom_t protocol)
{
    ensureAtoms();

    if (wmProtocols_ == XCB_ATOM_NONE || protocol == XCB_ATOM_NONE) {
        return false;
    }

    const auto cookie = xcb_get_property(
        connection_,
        0,
        client,
        wmProtocols_,
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
    const int count =
        xcb_get_property_value_length(reply) /
        static_cast<int>(sizeof(xcb_atom_t));

    bool supports = false;

    for (int index = 0; index < count; ++index) {
        if (atoms[index] == protocol) {
            supports = true;
            break;
        }
    }

    std::free(reply);
    return supports;
}

bool WindowManager::clientAcceptsClose(xcb_window_t client)
{
    return clientSupportsProtocol(client, wmDeleteWindow_);
}

void WindowManager::focusWindow(ManagedWindow& window, xcb_timestamp_t time)
{
    if (window.minimized) {
        window.minimized = false;
        xcb_map_window(connection_, window.frame);
    }

    const xcb_window_t previous = focusedFrame_;
    const bool changed = previous != window.frame;

    const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};

    xcb_configure_window(
        connection_,
        window.frame,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stackMode);

    if (window.acceptsInput) {
        xcb_set_input_focus(
            connection_,
            XCB_INPUT_FOCUS_POINTER_ROOT,
            window.client,
            time);
    }

    if (clientSupportsProtocol(window.client, wmTakeFocus_)) {
        xcb_client_message_event_t message = {};
        message.response_type = XCB_CLIENT_MESSAGE;
        message.format = 32;
        message.window = window.client;
        message.type = wmProtocols_;
        message.data.data32[0] = wmTakeFocus_;
        message.data.data32[1] = time;

        xcb_send_event(
            connection_,
            0,
            window.client,
            XCB_EVENT_MASK_NO_EVENT,
            reinterpret_cast<const char*>(&message));
    }

    focusedFrame_ = window.frame;
    rememberFocus(window.client);

    if (changed) {
        if (ManagedWindow* previousWindow = findByFrame(previous)) {
            drawTitleBar(previousWindow->titleBar);
        }

        drawTitleBar(window.titleBar);
    }

    raiseTransients(window.client);
    publishClientState(window);
    publishClientList();
    raisePanel();
    drawPanel();
    xcb_flush(connection_);
}

void WindowManager::clearFocus()
{
    const xcb_window_t previous = focusedFrame_;
    focusedFrame_ = XCB_WINDOW_NONE;

    if (!isConnected()) {
        return;
    }

    xcb_set_input_focus(
        connection_,
        XCB_INPUT_FOCUS_POINTER_ROOT,
        screen_->root,
        XCB_CURRENT_TIME);

    if (ManagedWindow* previousWindow = findByFrame(previous)) {
        drawTitleBar(previousWindow->titleBar);
    }

    publishClientList();
    drawPanel();
    xcb_flush(connection_);
}

void WindowManager::closeWindow(ManagedWindow& window)
{
    if (draggingFrame_ == window.frame) {
        draggingFrame_ = XCB_WINDOW_NONE;
        xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
    }

    if (!clientAcceptsClose(window.client)) {
        xcb_destroy_window(connection_, window.client);
        xcb_flush(connection_);
        return;
    }

    xcb_client_message_event_t message = {};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = window.client;
    message.type = wmProtocols_;
    message.data.data32[0] = wmDeleteWindow_;
    message.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(
        connection_,
        0,
        window.client,
        XCB_EVENT_MASK_NO_EVENT,
        reinterpret_cast<const char*>(&message));

    xcb_flush(connection_);
}

void WindowManager::minimizeWindow(ManagedWindow& window)
{
    if (draggingFrame_ == window.frame) {
        draggingFrame_ = XCB_WINDOW_NONE;
        xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
    }

    const bool wasFocused = focusedFrame_ == window.frame;
    window.minimized = true;

    xcb_unmap_window(connection_, window.frame);
    publishClientState(window);

    if (wasFocused) {
        clearFocus();
        focusAnotherVisible(window.client);
    } else {
        drawPanel();
    }

    xcb_flush(connection_);
}

void WindowManager::layoutFrame(
    ManagedWindow& window,
    int16_t x,
    int16_t y,
    uint16_t width,
    uint16_t height,
    bool honorMaximum)
{
    const FrameSize size = clampFrameSize(
        width,
        height,
        window.minimumWidth,
        window.minimumHeight,
        window.maximumWidth,
        window.maximumHeight,
        honorMaximum);

    width = size.width;
    height = size.height;

    const uint32_t frameValues[] = {
        packedCoordinate(x),
        packedCoordinate(y),
        width,
        height
    };

    xcb_configure_window(
        connection_,
        window.frame,
        XCB_CONFIG_WINDOW_X |
            XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT,
        frameValues);

    const uint32_t titleBarValues[] = {
        0u,
        0u,
        width,
        kTitleBarHeight
    };

    xcb_configure_window(
        connection_,
        window.titleBar,
        XCB_CONFIG_WINDOW_X |
            XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT,
        titleBarValues);

    const uint16_t clientHeight =
        height > kTitleBarHeight
            ? static_cast<uint16_t>(height - kTitleBarHeight)
            : static_cast<uint16_t>(1);

    const uint32_t clientValues[] = {
        0u,
        kTitleBarHeight,
        width,
        clientHeight,
        0u
    };

    xcb_configure_window(
        connection_,
        window.client,
        XCB_CONFIG_WINDOW_X |
            XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT |
            XCB_CONFIG_WINDOW_BORDER_WIDTH,
        clientValues);

    const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};

    xcb_configure_window(
        connection_,
        window.titleBar,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stackMode);

    sendConfigureNotify(window, x, y, width, height);
}

void WindowManager::toggleMaximize(ManagedWindow& window)
{
    if (!isConnected()) {
        return;
    }

    if (window.maximized) {
        layoutFrame(
            window,
            window.restoreX,
            window.restoreY,
            window.restoreWidth,
            window.restoreHeight);

        window.maximized = false;
        publishClientState(window);
        xcb_flush(connection_);
        drawTitleBar(window.titleBar);
        drawPanel();
        return;
    }

    const auto cookie = xcb_get_geometry(connection_, window.frame);
    xcb_generic_error_t* error = nullptr;
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, cookie, &error);

    std::free(error);

    if (geometry == nullptr) {
        return;
    }

    window.restoreX = geometry->x;
    window.restoreY = geometry->y;
    window.restoreWidth = geometry->width;
    window.restoreHeight = geometry->height;
    std::free(geometry);

    const MonitorRect area = workArea();

    layoutFrame(
        window,
        area.x,
        area.y,
        area.width,
        area.height,
        false);

    window.maximized = true;
    publishClientState(window);
    xcb_flush(connection_);
    drawTitleBar(window.titleBar);
    drawPanel();
}
