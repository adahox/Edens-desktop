#include <gtk/gtk.h>

GtkWidget *build_action_bar(void)
{
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *status = gtk_label_new("0 itens");

    gtk_widget_set_name(action_bar, "action-bar");
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), status);

    return action_bar;
}