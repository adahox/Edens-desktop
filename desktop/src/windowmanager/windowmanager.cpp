#include "windowmanager.hpp"
#include "titlebarlayout.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#undef None
#undef Status
#undef Bool
#undef True
#undef False
#undef Success

#include <cairo/cairo.h>
#include <jpeglib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <cerrno>
#include <sys/select.h>
#include <unistd.h>

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
constexpr xcb_keycode_t kKeySuperL = 133;
constexpr xcb_keycode_t kKeySuperR = 134;

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

struct Wallpaper
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
    bool ready = false;
};

struct JpegFailure
{
    jpeg_error_mgr base;
    jmp_buf jump;
};

void jpegAbort(j_common_ptr info)
{
    auto* failure = reinterpret_cast<JpegFailure*>(info->err);
    longjmp(failure->jump, 1);
}

std::string executableDirectory()
{
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

    if (length <= 0) {
        return {};
    }

    buffer[length] = '\0';
    const std::string path(buffer);
    const auto slash = path.find_last_of('/');

    if (slash == std::string::npos) {
        return {};
    }

    return path.substr(0, slash);
}

bool loadJpeg(const std::string& path, Wallpaper& image)
{
    FILE* file = std::fopen(path.c_str(), "rb");

    if (file == nullptr) {
        return false;
    }

    jpeg_decompress_struct info{};
    JpegFailure failure{};
    info.err = jpeg_std_error(&failure.base);
    failure.base.error_exit = jpegAbort;

    if (setjmp(failure.jump) != 0) {
        jpeg_destroy_decompress(&info);
        std::fclose(file);
        return false;
    }

    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    image.width = static_cast<int>(info.output_width);
    image.height = static_cast<int>(info.output_height);
    image.rgb.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 3, 0);

    while (info.output_scanline < info.output_height) {
        std::uint8_t* row = image.rgb.data() +
            static_cast<std::size_t>(info.output_scanline) * static_cast<std::size_t>(image.width) * 3;
        jpeg_read_scanlines(&info, &row, 1);
    }

    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    std::fclose(file);
    return image.width > 0 && image.height > 0;
}

Wallpaper& desktopWallpaper()
{
    static Wallpaper image;

    if (image.ready || image.width < 0) {
        return image;
    }

    const std::string directory = executableDirectory();
    const std::string candidates[] = {
        directory + "/../assets/wallpapers/default.jpg",
        directory + "/assets/wallpapers/default.jpg"
    };

    for (const std::string& candidate : candidates) {
        if (loadJpeg(candidate, image)) {
            image.ready = true;
            return image;
        }
    }

    image.width = -1;
    return image;
}

std::vector<MonitorRect> queryMonitorRects(int fallbackWidth, int fallbackHeight)
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

    if (monitors.empty()) {
        monitors.push_back(MonitorRect{
            0,
            0,
            static_cast<uint16_t>(std::max(fallbackWidth, 1)),
            static_cast<uint16_t>(std::max(fallbackHeight, 1))
        });
    }

    return monitors;
}

void wallpaperPixel(
    const Wallpaper& image,
    int screenX,
    int screenY,
    int screenWidth,
    int screenHeight,
    std::uint8_t& red,
    std::uint8_t& green,
    std::uint8_t& blue)
{
    static const std::vector<MonitorRect> monitors =
        queryMonitorRects(screenWidth, screenHeight);
    const int monitorIndex = monitorIndexAt(
        monitors,
        static_cast<int16_t>(screenX),
        static_cast<int16_t>(screenY));
    const MonitorRect& monitor = monitors[
        static_cast<std::size_t>(std::max(monitorIndex, 0))];
    const int localX = screenX - monitor.x;
    const int localY = screenY - monitor.y;
    const int monitorWidth = std::max<int>(1, monitor.width);
    const int monitorHeight = std::max<int>(1, monitor.height);

    const double scale = std::max(
        static_cast<double>(monitorWidth) / image.width,
        static_cast<double>(monitorHeight) / image.height);
    const double originX = (image.width * scale - monitorWidth) / 2.0;
    const double originY = (image.height * scale - monitorHeight) / 2.0;
    int imageX = static_cast<int>((originX + localX) / scale);
    int imageY = static_cast<int>((originY + localY) / scale);
    imageX = std::clamp(imageX, 0, image.width - 1);
    imageY = std::clamp(imageY, 0, image.height - 1);
    const std::uint8_t* pixel = image.rgb.data() +
        (static_cast<std::size_t>(imageY) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(imageX)) * 3;
    red = pixel[0];
    green = pixel[1];
    blue = pixel[2];
}

bool snowDarkTheme()
{
    using clock = std::chrono::steady_clock;
    static clock::time_point checked{};
    static bool dark = false;
    const auto now = clock::now();

    if (checked != clock::time_point{} && now - checked < std::chrono::seconds(1)) {
        return dark;
    }

    checked = now;
    dark = false;
    const char *home = std::getenv("HOME");

    if (home == nullptr) {
        return false;
    }

    std::ifstream file(std::string(home) + "/.config/eden/glass");
    std::string line;

    while (std::getline(file, line)) {
        if (line.rfind("dark=", 0) == 0) {
            dark = line.size() > 5 && line[5] == '1';
        }
    }

    return dark;
}

std::uint8_t mixToward(std::uint8_t color, std::uint8_t target, int alpha)
{
    return static_cast<std::uint8_t>((color * (255 - alpha) + target * alpha) / 255);
}

bool fillTitleGlass(
    const xcb_screen_t* screen,
    int16_t rootX,
    int16_t rootY,
    uint16_t width,
    uint16_t height,
    bool focused,
    std::vector<std::uint8_t>& pixels)
{
    const Wallpaper& image = desktopWallpaper();

    if (!image.ready || width == 0 || height == 0 || screen->root_depth < 24) {
        return false;
    }

    const int columns = std::max(1, static_cast<int>(width) / 10);
    const int rows = std::max(1, static_cast<int>(height) / 6);
    std::vector<std::uint8_t> coarse(static_cast<std::size_t>(columns * rows * 3));

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int sampleX = rootX + column * static_cast<int>(width) / columns;
            const int sampleY = rootY + row * static_cast<int>(height) / rows;
            std::uint8_t red = 0;
            std::uint8_t green = 0;
            std::uint8_t blue = 0;
            wallpaperPixel(
                image,
                sampleX,
                sampleY,
                screen->width_in_pixels,
                screen->height_in_pixels,
                red,
                green,
                blue);
            std::uint8_t* pixel = coarse.data() + static_cast<std::size_t>((row * columns + column) * 3);
            pixel[0] = red;
            pixel[1] = green;
            pixel[2] = blue;
        }
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    pixels.assign(stride * height, 0);
    const bool dark = snowDarkTheme();
    const int veil = focused ? 196 : 217;
    const std::uint8_t snowRed = dark ? 18 : 244;
    const std::uint8_t snowGreen = dark ? 24 : 247;
    const std::uint8_t snowBlue = dark ? 32 : 251;

    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(rows) /
            static_cast<float>(height) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, rows - 1);
        const int y1 = std::min(y0 + 1, rows - 1);
        const float yBlend = std::clamp(fy - static_cast<float>(y0), 0.f, 1.f);
        const int sheen = y < height / 3 ? 36 : 12;

        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(columns) /
                static_cast<float>(width) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, columns - 1);
            const int x1 = std::min(x0 + 1, columns - 1);
            const float xBlend = std::clamp(fx - static_cast<float>(x0), 0.f, 1.f);
            const std::uint8_t* p00 = coarse.data() + static_cast<std::size_t>((y0 * columns + x0) * 3);
            const std::uint8_t* p10 = coarse.data() + static_cast<std::size_t>((y0 * columns + x1) * 3);
            const std::uint8_t* p01 = coarse.data() + static_cast<std::size_t>((y1 * columns + x0) * 3);
            const std::uint8_t* p11 = coarse.data() + static_cast<std::size_t>((y1 * columns + x1) * 3);
            std::uint8_t* destination = pixels.data() + static_cast<std::size_t>(y) * stride +
                static_cast<std::size_t>(x) * 4;

            for (int channel = 0; channel < 3; ++channel) {
                const float top = p00[channel] + (p10[channel] - p00[channel]) * xBlend;
                const float bottom = p01[channel] + (p11[channel] - p01[channel]) * xBlend;
                const auto blended = static_cast<std::uint8_t>(top + (bottom - top) * yBlend);
                const std::uint8_t snow = channel == 0 ? snowRed : channel == 1 ? snowGreen : snowBlue;
                destination[channel] = mixToward(mixToward(blended, snow, veil), 255, sheen);
            }

            destination[3] = 0;
            const std::uint8_t red = destination[0];
            destination[0] = destination[2];
            destination[2] = red;
        }
    }

    return true;
}

void fillSolidBar(
    uint16_t width,
    uint16_t height,
    bool focused,
    std::vector<std::uint8_t>& pixels)
{
    const bool dark = snowDarkTheme();
    const std::uint8_t red = dark ? 0x12 : 0xF4;
    const std::uint8_t green = dark ? (focused ? 0x1C : 0x18) : 0xF7;
    const std::uint8_t blue = dark ? (focused ? 0x24 : 0x20) : 0xFB;
    pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);

    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = blue;
        pixels[index + 1] = green;
        pixels[index + 2] = red;
    }
}

void roundedRect(cairo_t* context, double x, double y, double width, double height, double radius)
{
    cairo_new_sub_path(context);
    cairo_arc(context, x + width - radius, y + radius, radius, -1.5707963, 0);
    cairo_arc(context, x + width - radius, y + height - radius, radius, 0, 1.5707963);
    cairo_arc(context, x + radius, y + height - radius, radius, 1.5707963, 3.1415927);
    cairo_arc(context, x + radius, y + radius, radius, 3.1415927, 4.712389);
    cairo_close_path(context);
}

std::string titleThatFits(cairo_t* context, std::string title, double maxWidth)
{
    cairo_text_extents_t extents{};
    cairo_text_extents(context, title.c_str(), &extents);

    if (extents.x_advance <= maxWidth) {
        return title;
    }

    while (!title.empty()) {
        title.pop_back();

        while (!title.empty() &&
               (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) {
            title.pop_back();
        }

        const std::string candidate = title + "...";
        cairo_text_extents(context, candidate.c_str(), &extents);

        if (extents.x_advance <= maxWidth) {
            return candidate;
        }
    }

    return {};
}

void strokeIcon(cairo_t* context, double red, double green, double blue)
{
    cairo_set_source_rgb(context, red, green, blue);
    cairo_set_line_width(context, 1.6);
    cairo_set_line_cap(context, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(context, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(context);
}

void paintHoverBackground(
    cairo_t* context,
    const TitleButtonLayout& button,
    bool glass)
{
    (void)glass;
    roundedRect(
        context,
        button.x + 0.5,
        button.y + 0.5,
        kButtonSize - 1.0,
        kButtonSize - 1.0,
        5.0);

    cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 0.12);

    cairo_fill(context);
}

void paintTitleChrome(
    std::vector<std::uint8_t>& pixels,
    uint16_t width,
    uint16_t height,
    const std::string& title,
    bool focused,
    bool maximized,
    bool glass,
    TitleButton hovered)
{
    if (width == 0 || height == 0 || pixels.size() < static_cast<std::size_t>(width) * height * 4) {
        return;
    }

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        pixels.data(),
        CAIRO_FORMAT_RGB24,
        width,
        height,
        static_cast<int>(static_cast<std::size_t>(width) * 4));

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }

    cairo_t* context = cairo_create(surface);
    (void)glass;
    const bool dark = snowDarkTheme();
    const double ink = dark ? (focused ? 0.93 : 0.68) : (focused ? 0.11 : 0.34);

    if (focused) {
        cairo_set_source_rgb(context, 0.0, 0.898, 1.0);
        cairo_rectangle(context, 14, (height - 14) / 2.0, 3, 14);
        cairo_fill(context);
    }

    if (dark) {
        cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 0.15);
    } else {
        cairo_set_source_rgba(context, 0.07, 0.10, 0.13, 0.16);
    }
    cairo_rectangle(context, 0, height - 1.0, width, 1);
    cairo_fill(context);

    const TitleBarButtons buttons = titleBarButtons(width);
    cairo_select_font_face(
        context,
        "Cantarell",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context, 13);
    const double textLimit = std::max(0.0, static_cast<double>(buttons.minimize.x) - 30.0);
    const std::string fitted = titleThatFits(context, title, textLimit);
    cairo_text_extents_t extents{};
    cairo_text_extents(context, fitted.c_str(), &extents);
    cairo_set_source_rgb(context, ink, ink, ink);
    cairo_move_to(
        context,
        22,
        (height - extents.height) / 2.0 - extents.y_bearing);
    cairo_show_text(context, fitted.c_str());

    const auto center = [](const TitleButtonLayout& button) {
        return std::pair<double, double>{
            button.x + kButtonSize / 2.0,
            button.y + kButtonSize / 2.0
        };
    };

    if (hovered == TitleButton::Minimize) {
        paintHoverBackground(context, buttons.minimize, glass);
    }

    if (hovered == TitleButton::Maximize) {
        paintHoverBackground(context, buttons.maximize, glass);
    }

    const auto [minimizeX, minimizeY] = center(buttons.minimize);
    cairo_move_to(context, minimizeX - 5.0, minimizeY);
    cairo_line_to(context, minimizeX + 5.0, minimizeY);
    strokeIcon(context, ink, ink, ink);

    const auto [maximizeX, maximizeY] = center(buttons.maximize);
    cairo_set_source_rgb(context, ink, ink, ink);
    cairo_set_line_width(context, 1.5);

    if (maximized) {
        cairo_rectangle(context, maximizeX - 1.5, maximizeY - 4.5, 7, 7);
        cairo_stroke(context);
        cairo_rectangle(context, maximizeX - 4.5, maximizeY - 1.5, 7, 7);
        cairo_stroke(context);
    } else {
        cairo_rectangle(context, maximizeX - 4.5, maximizeY - 4.5, 9, 9);
        cairo_stroke(context);
    }

    const double closeFill = hovered == TitleButton::Close ? 0.945 : 0.820;
    const double closeGreen = hovered == TitleButton::Close ? 0.290 : 0.416;
    const double closeBlue = hovered == TitleButton::Close ? 0.290 : 0.416;
    const double closeX = buttons.close.x + 1.0;
    const double closeY = buttons.close.y + 1.0;
    roundedRect(context, closeX, closeY, 16, 16, 4);
    cairo_set_source_rgb(context, closeFill, closeGreen, closeBlue);
    cairo_fill(context);

    const double closeCenterX = buttons.close.x + kButtonSize / 2.0;
    const double closeCenterY = buttons.close.y + kButtonSize / 2.0;
    cairo_move_to(context, closeCenterX - 3.2, closeCenterY - 3.2);
    cairo_line_to(context, closeCenterX + 3.2, closeCenterY + 3.2);
    cairo_move_to(context, closeCenterX + 3.2, closeCenterY - 3.2);
    cairo_line_to(context, closeCenterX - 3.2, closeCenterY + 3.2);
    strokeIcon(context, 1, 1, 1);

    cairo_destroy(context);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
}

xcb_cursor_t loadLeftPointer(xcb_connection_t* connection)
{
    constexpr char kCursorFont[] = "cursor";
    constexpr uint16_t kLeftPointer = 68;

    const xcb_font_t font = xcb_generate_id(connection);
    const auto opened = xcb_open_font_checked(
        connection,
        font,
        static_cast<uint16_t>(sizeof(kCursorFont) - 1),
        kCursorFont);
    xcb_generic_error_t* error = xcb_request_check(connection, opened);

    if (error != nullptr) {
        std::free(error);
        return XCB_NONE;
    }

    const xcb_cursor_t cursor = xcb_generate_id(connection);
    const auto created = xcb_create_glyph_cursor_checked(
        connection,
        cursor,
        font,
        font,
        kLeftPointer,
        static_cast<uint16_t>(kLeftPointer + 1),
        0,
        0,
        0,
        65535,
        65535,
        65535);
    error = xcb_request_check(connection, created);
    xcb_close_font(connection, font);

    if (error != nullptr) {
        std::free(error);
        return XCB_NONE;
    }

    return cursor;
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
    pointerCursor_ = loadLeftPointer(connection_);

    if (pointerCursor_ != XCB_NONE) {
        xcb_change_window_attributes(
            connection_,
            screen_->root,
            XCB_CW_CURSOR,
            &pointerCursor_);
        xcb_flush(connection_);
    }

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

    grabHotkey(connection_, screen_->root, 0, kKeySuperL);
    grabHotkey(connection_, screen_->root, 0, kKeySuperR);
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

    // Escanear janelas já existentes na tela antes do Eden rodar.
    // As barras reservam espaço antes das janelas serem decoradas.
    const auto treeCookie = xcb_query_tree(connection_, screen_->root);
    xcb_query_tree_reply_t* tree = xcb_query_tree_reply(connection_, treeCookie, nullptr);

    if (tree != nullptr) {
        const int count = xcb_query_tree_children_length(tree);
        xcb_window_t* children = xcb_query_tree_children(tree);
        std::vector<xcb_window_t> clientsToManage;
        clientsToManage.reserve(static_cast<std::size_t>(count));

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
                    clientsToManage.push_back(children[i]);
                }

                std::free(attr);
            }
        }

        std::free(tree);

        for (const xcb_window_t client : clientsToManage) {
            manageWindow(client);
        }
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

    if (connection_ != nullptr &&
        pointerCursor_ != XCB_NONE &&
        xcb_connection_has_error(connection_) == 0) {
        xcb_free_cursor(connection_, pointerCursor_);
    }

    pointerCursor_ = XCB_NONE;
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
    refreshScreenLayoutIfNeeded();

    std::cout << "Eden WindowManager ativo. Pressione Ctrl+C para sair.\n";

    while (running_) {
        refreshScreenLayoutIfNeeded();

        const int fd = xcb_get_file_descriptor(connection_);
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 400000;

        const int ready = select(fd + 1, &readSet, nullptr, nullptr, &timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "Falha ao aguardar eventos X11.\n";
            break;
        }

        xcb_generic_event_t* event = nullptr;

        while ((event = xcb_poll_for_event(connection_)) != nullptr) {
            processMessages(event);
            std::free(event);
        }

        if (xcb_connection_has_error(connection_) != 0) {
            std::cerr << "A conexão X11 foi encerrada.\n";
            break;
        }
    }

    if (keyboardGrabbed_ && isConnected()) {
        xcb_ungrab_keyboard(connection_, XCB_CURRENT_TIME);
        keyboardGrabbed_ = false;
        xcb_flush(connection_);
    }

    superHeld_ = false;
    superChord_ = false;
    running_ = false;
}

void WindowManager::stop()
{
    // Signal-safe: only flips the run flag. Cleanup happens when run() exits.
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
    const bool wasMapped = attributes->map_state != XCB_MAP_STATE_UNMAPPED;
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
    int16_t y = geometry->y;

    std::free(geometry);

    const bool decorated = clientWantsTitleBar(client);
    const uint16_t chrome = clientTopOffset(decorated);

    const MonitorRect area = workAreaAt(x, y);

    if (y < area.y) {
        y = area.y;
    }

    const xcb_window_t frame = xcb_generate_id(connection_);
    // CSD clients often leave transparent margins; an opaque black frame
    // behind them looks like the app is trapped in a black box.
    uint32_t frameMask = XCB_CW_BORDER_PIXEL |
        XCB_CW_EVENT_MASK |
        (pointerCursor_ != XCB_NONE ? XCB_CW_CURSOR : 0);
    uint32_t frameValues[5];
    int frameValueCount = 0;

    if (decorated) {
        frameMask |= XCB_CW_BACK_PIXEL;
        frameValues[frameValueCount++] = screen_->black_pixel;
    } else {
        frameMask |= XCB_CW_BACK_PIXMAP;
        frameValues[frameValueCount++] = XCB_NONE;
    }

    frameValues[frameValueCount++] = borderUnfocusedPixel_;
    frameValues[frameValueCount++] =
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    if (pointerCursor_ != XCB_NONE) {
        frameValues[frameValueCount++] = pointerCursor_;
    }

    xcb_create_window(
        connection_,
        XCB_COPY_FROM_PARENT,
        frame,
        screen_->root,
        x,
        y,
        width,
        frameHeightForClient(height, decorated),
        kFrameBorderWidth,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen_->root_visual,
        frameMask,
        frameValues
    );

    const xcb_window_t titleBar = xcb_generate_id(connection_);
    const uint32_t titleBarValues[] = {
        screen_->black_pixel,
        XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_ENTER_WINDOW |
            XCB_EVENT_MASK_LEAVE_WINDOW,
        pointerCursor_
    };
    const uint32_t titleBarMask = XCB_CW_BACK_PIXEL |
        XCB_CW_EVENT_MASK |
        (pointerCursor_ != XCB_NONE ? XCB_CW_CURSOR : 0);

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
        titleBarMask,
        titleBarValues
    );

    xcb_reparent_window(
        connection_,
        client,
        frame,
        0,
        chrome
    );

    const uint32_t clientEventMask =
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_PROPERTY_CHANGE |
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

    if (decorated) {
        xcb_map_window(connection_, titleBar);
    }

    xcb_map_window(connection_, client);
    xcb_map_window(connection_, frame);

    if (decorated) {
        const uint32_t titleAbove[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(
            connection_,
            titleBar,
            XCB_CONFIG_WINDOW_STACK_MODE,
            titleAbove);
    }

    ManagedWindow& managed =
        windows_.emplace(client, ManagedWindow{client, frame, titleBar})
            .first->second;

    managed.decorated = decorated;
    managed.desktop = currentDesktop_;
    managed.ignoreUnmap = wasMapped ? 1 : 0;
    readWindowHints(managed);

    // Explicit sync after reparent so the client fills the frame content area.
    layoutFrame(
        managed,
        x,
        y,
        width,
        frameHeightForClient(height, managed.decorated),
        true);
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
        << (decorated ? "Janela decorada: " : "Janela sem barra (CSD): ")
        << client
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

    if (hoveredTitleBar_ == iterator->second.titleBar) {
        hoveredTitleBar_ = XCB_WINDOW_NONE;
        hoveredButton_ = TitleButton::None;
    }

    if (lastTitleClickBar_ == iterator->second.titleBar) {
        lastTitleClickBar_ = XCB_WINDOW_NONE;
        lastTitleClickTime_ = 0;
    }

    if (draggingFrame_ == frame || resizingFrame_ == frame) {
        draggingFrame_ = XCB_WINDOW_NONE;
        resizingFrame_ = XCB_WINDOW_NONE;
        resizingDirection_ = ResizeDirection::None;
        xcb_ungrab_pointer(connection_, XCB_CURRENT_TIME);
    }

    const bool decorated = iterator->second.decorated;
    int16_t x = 0;
    int16_t y = 0;
    const auto geometryCookie = xcb_get_geometry(connection_, frame);
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, geometryCookie, nullptr);

    if (geometry != nullptr) {
        x = geometry->x;
        y = static_cast<int16_t>(
            geometry->y + clientTopOffset(decorated));
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
    const ManagedWindow* managed = findByTitleBar(titleBar);

    if (managed != nullptr && !managed->decorated) {
        return;
    }

    const auto geometryCookie = xcb_get_geometry(connection_, titleBar);

    xcb_generic_error_t* error = nullptr;
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(connection_, geometryCookie, &error);

    std::free(error);

    if (geometry == nullptr) {
        return;
    }

    const bool maximized = managed != nullptr && managed->maximized;
    const bool focused =
        managed != nullptr && managed->frame == focusedFrame_;

    const xcb_gcontext_t gc = xcb_generate_id(connection_);
    const uint32_t values[] = {0};
    xcb_create_gc(connection_, gc, titleBar, XCB_GC_GRAPHICS_EXPOSURES, values);

    const auto positionCookie = xcb_translate_coordinates(
        connection_,
        titleBar,
        screen_->root,
        0,
        0);
    xcb_translate_coordinates_reply_t* position =
        xcb_translate_coordinates_reply(connection_, positionCookie, nullptr);
    const int16_t rootX = position == nullptr ? 0 : position->dst_x;
    const int16_t rootY = position == nullptr ? 0 : position->dst_y;
    std::free(position);

    std::vector<std::uint8_t> pixels;
    const bool glass = fillTitleGlass(
        screen_,
        rootX,
        rootY,
        geometry->width,
        geometry->height,
        focused,
        pixels);

    if (!glass) {
        fillSolidBar(geometry->width, geometry->height, focused, pixels);
    }

    std::string title = "Eden Window";

    if (managed != nullptr && !managed->title.empty()) {
        title = managed->title;
    }

    const TitleButton hovered =
        titleBar == hoveredTitleBar_ ? hoveredButton_ : TitleButton::None;

    paintTitleChrome(
        pixels,
        geometry->width,
        geometry->height,
        title,
        focused,
        maximized,
        glass,
        hovered);

    if (screen_->root_depth >= 24 && !pixels.empty()) {
        xcb_put_image(
            connection_,
            XCB_IMAGE_FORMAT_Z_PIXMAP,
            titleBar,
            gc,
            geometry->width,
            geometry->height,
            0,
            0,
            0,
            screen_->root_depth,
            static_cast<std::uint32_t>(pixels.size()),
            pixels.data());
    }

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

void WindowManager::raiseFrame(ManagedWindow& window)
{
    if (!isConnected() || window.frame == XCB_WINDOW_NONE) {
        return;
    }

    const uint32_t top[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(
        connection_,
        window.frame,
        XCB_CONFIG_WINDOW_STACK_MODE,
        top);
}

bool WindowManager::allowClientRestack(const ManagedWindow& window) const
{
    // Only the focused window may raise itself. Otherwise a maximized app that
    // loses focus can XRaiseWindow and cover the newly activated one.
    return window.frame != XCB_WINDOW_NONE && window.frame == focusedFrame_;
}

void WindowManager::focusWindow(ManagedWindow& window, xcb_timestamp_t time)
{
    if (window.minimized) {
        window.minimized = false;
        mapClientFrame(window);
    }

    const xcb_window_t previous = focusedFrame_;
    const bool changed = previous != window.frame;

    // Mark focus before raise so allowClientRestack accepts this window's own
    // follow-up ConfigureRequests, and rejects the previous top window's.
    focusedFrame_ = window.frame;
    raiseFrame(window);

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

    rememberFocus(window.client);

    if (changed) {
        if (ManagedWindow* previousWindow = findByFrame(previous)) {
            updateFrameBorder(*previousWindow, false);

            if (previousWindow->decorated) {
                drawTitleBar(previousWindow->titleBar);
            }
        }

        updateFrameBorder(window, true);

        if (window.decorated) {
            drawTitleBar(window.titleBar);
        }
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
        updateFrameBorder(*previousWindow, false);

        if (previousWindow->decorated) {
            drawTitleBar(previousWindow->titleBar);
        }
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

    const MonitorRect area = workAreaAt(x, y);

    if (y < area.y) {
        y = area.y;
    }

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

    const uint16_t chrome = clientTopOffset(window.decorated);

    if (window.decorated) {
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

        const uint32_t stackMode[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(
            connection_,
            window.titleBar,
            XCB_CONFIG_WINDOW_STACK_MODE,
            stackMode);
    }

    const uint16_t clientHeight =
        height > chrome
            ? static_cast<uint16_t>(height - chrome)
            : static_cast<uint16_t>(1);

    const uint32_t clientValues[] = {
        0u,
        chrome,
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

    sendConfigureNotify(window, x, y, width, height);
    tightWrapFrame(window);
}

void WindowManager::tightWrapFrame(ManagedWindow& window)
{
    if (!isConnected() || window.maximized) {
        return;
    }

    xcb_flush(connection_);

    const auto clientCookie = xcb_get_geometry(connection_, window.client);
    const auto frameCookie = xcb_get_geometry(connection_, window.frame);
    xcb_get_geometry_reply_t* clientGeometry =
        xcb_get_geometry_reply(connection_, clientCookie, nullptr);
    xcb_get_geometry_reply_t* frameGeometry =
        xcb_get_geometry_reply(connection_, frameCookie, nullptr);

    if (clientGeometry == nullptr || frameGeometry == nullptr) {
        std::free(clientGeometry);
        std::free(frameGeometry);
        return;
    }

    const uint16_t chrome = clientTopOffset(window.decorated);
    const uint16_t contentHeight =
        frameGeometry->height > chrome
            ? static_cast<uint16_t>(frameGeometry->height - chrome)
            : static_cast<uint16_t>(1);

    const bool clientSmaller =
        clientGeometry->width + 1 < frameGeometry->width ||
        clientGeometry->height + 1 < contentHeight;

    if (!clientSmaller) {
        std::free(clientGeometry);
        std::free(frameGeometry);
        return;
    }

    const uint16_t wrappedWidth = std::max<uint16_t>(1, clientGeometry->width);
    const uint16_t wrappedHeight = frameHeightForClient(
        std::max<uint16_t>(1, clientGeometry->height),
        window.decorated);

    const uint32_t frameValues[] = {
        packedCoordinate(frameGeometry->x),
        packedCoordinate(frameGeometry->y),
        wrappedWidth,
        wrappedHeight
    };

    xcb_configure_window(
        connection_,
        window.frame,
        XCB_CONFIG_WINDOW_X |
            XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT,
        frameValues);

    if (window.decorated) {
        const uint32_t titleBarValues[] = {
            0u,
            0u,
            wrappedWidth,
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
    }

    const uint32_t clientValues[] = {
        0u,
        chrome,
        wrappedWidth,
        std::max<uint16_t>(1, clientGeometry->height),
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

    sendConfigureNotify(
        window,
        frameGeometry->x,
        frameGeometry->y,
        wrappedWidth,
        wrappedHeight);

    std::free(clientGeometry);
    std::free(frameGeometry);
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

    const int16_t anchorX = static_cast<int16_t>(
        window.restoreX + static_cast<int16_t>(window.restoreWidth / 2));
    const int16_t anchorY = static_cast<int16_t>(
        window.restoreY + static_cast<int16_t>(window.restoreHeight / 2));
    const MonitorRect area = workAreaAt(anchorX, anchorY);

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
