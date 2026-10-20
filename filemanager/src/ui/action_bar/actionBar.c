#include <gtk/gtk.h>
#include "ui/action_bar/actionBar.h"

#define ACTION_BAR_STATUS_KEY "edens-action-status"

GtkWidget *build_action_bar(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(root, "action-bar");

    GtkWidget *status = gtk_label_new("0 itens");
    gtk_widget_set_name(status, "status");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0);
    gtk_box_pack_start(GTK_BOX(root), status, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(root), ACTION_BAR_STATUS_KEY, status);

    return root;
}

GtkWidget *action_bar_get_status_label(GtkWidget *action_bar)
{
    return g_object_get_data(G_OBJECT(action_bar), ACTION_BAR_STATUS_KEY);
}
