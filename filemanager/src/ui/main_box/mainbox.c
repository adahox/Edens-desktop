#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/main_box/mainbox.h"

GtkWidget *build_main_box(void)
{
    EWidget *main_box = make(GTK_TYPE_BOX, "main_box");

    add_prop(main_box, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(main_box, "spacing", 0);

    return GTK_WIDGET(main_box->object);
}
