#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/fast_directories/fast_directories.h"

GtkWidget *build_fast_directories(void)
{
    EWidget *sidebar = make(GTK_TYPE_BOX, "fast_directories");

    add_prop(sidebar, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(sidebar, "spacing", 8);

    return GTK_WIDGET(sidebar->object);
}
