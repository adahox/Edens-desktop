#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/content/content.h"

GtkWidget *build_content(GtkWidget *scrolled_window)
{
    GtkWidget *sheet = gtk_event_box_new();
    gtk_widget_set_name(sheet, "file-sheet");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(sheet), TRUE);
    gtk_widget_set_hexpand(sheet, TRUE);
    gtk_widget_set_vexpand(sheet, TRUE);

    EWidget *content = make(GTK_TYPE_BOX, "content");

    add_prop(content, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(content, "spacing", 0);

    gtk_box_pack_start(
        GTK_BOX(content->object),
        scrolled_window,
        TRUE,
        TRUE,
        0
    );

    gtk_container_add(GTK_CONTAINER(sheet), GTK_WIDGET(content->object));

    return sheet;
}
