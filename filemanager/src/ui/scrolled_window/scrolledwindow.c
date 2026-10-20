#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/scrolled_window/scrolledwindow.h"

GtkWidget *build_scrolled_window(void)
{
    EWidget *scrolled_window = make(GTK_TYPE_SCROLLED_WINDOW, "scrolled-window");

    add_prop(scrolled_window, "shadow-type", GTK_SHADOW_NONE);
    add_prop(scrolled_window, "hexpand", TRUE);
    add_prop(scrolled_window, "vexpand", TRUE);
    add_prop(scrolled_window, "hscrollbar-policy", GTK_POLICY_AUTOMATIC);
    add_prop(scrolled_window, "vscrollbar-policy", GTK_POLICY_AUTOMATIC);

    return GTK_WIDGET(scrolled_window->object);
}
