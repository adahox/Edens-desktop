#include <gtk/gtk.h>
#include "ui/window/window.h"
#include "ui/header_bar/header_bar.h"
#include "ui/menu/menu.h"
#include "ui/app_icon/app_icon.h"
#include "core/widgetFactory.h"
#include "core/widget/events.h"

static void on_window_realize(GtkWidget *window, gpointer user_data)
{
    (void)user_data;
    edens_window_set_icon(window);
}

static void set_window_properties(EWidget *window)
{
    add_prop(window, "resizable", TRUE);
    add_prop(window, "default-width", 1100);
    add_prop(window, "default-height", 720);

    add_event(
        window,
        "realize",
        G_CALLBACK(on_window_realize),
        NULL,
        NULL
    );
}

GtkWidget *build_window(GtkApplication *app)
{
    EWidget *window = make(GTK_TYPE_APPLICATION_WINDOW, "window");

    set_window_properties(window);

    GtkWidget *header_bar = create_header_bar();
    gtk_window_set_titlebar(GTK_WINDOW(window->object), header_bar);

    register_actions(app);

    GMenu *menu = create_menu();
    gtk_application_set_menubar(GTK_APPLICATION(app), G_MENU_MODEL(menu));
    g_object_unref(menu);

    gtk_application_window_set_show_menubar(
        GTK_APPLICATION_WINDOW(window->object),
        FALSE
    );

    gtk_application_add_window(app, GTK_WINDOW(window->object));

    return GTK_WIDGET(window->object);
}
