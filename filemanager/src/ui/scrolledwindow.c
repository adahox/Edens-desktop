#include <gtk/gtk.h>
#include "scrolledwindow.h"

GtkWidget *build_scrolled_window(void)
{
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);

    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled_window),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );

    return scrolled_window;
}
