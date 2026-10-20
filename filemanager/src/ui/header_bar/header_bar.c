#include <gtk/gtk.h>
#include "ui/header_bar/header_bar.h"
#include "core/widgetFactory.h"
#include "core/widget/events.h"

GtkWidget *create_header_bar(void)
{
    EWidget *header_bar = make(GTK_TYPE_HEADER_BAR, "header-bar");
    
    add_prop(header_bar, "show-close-button", TRUE);
    add_prop(header_bar, "title", "eden");

    return GTK_WIDGET(header_bar->object);
}