#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/content/content.h"

GtkWidget *build_content(GtkWidget *scrolled_window)
{
    EWidget *content = make(GTK_TYPE_BOX, "content");

    add_prop(content, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(content, "spacing", 8);

    gtk_box_pack_start(
        GTK_BOX(content->object),
        scrolled_window,
        TRUE,
        TRUE,
        0
    );

    return GTK_WIDGET(content->object);
}
