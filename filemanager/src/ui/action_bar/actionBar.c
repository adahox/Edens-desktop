#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/action_bar/actionBar.h"

GtkWidget *build_action_bar(void)
{
    EWidget *action_bar = make(GTK_TYPE_ACTION_BAR, "action-bar");
    EWidget *status = make(GTK_TYPE_LABEL, "status");

    add_prop(status, "label", "0 itens");

    gtk_action_bar_pack_start(
        GTK_ACTION_BAR(action_bar->object),
        GTK_WIDGET(status->object)
    );

    return GTK_WIDGET(action_bar->object);
}
