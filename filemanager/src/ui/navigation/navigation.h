#ifndef EDENS_NAVIGATION_H
#define EDENS_NAVIGATION_H

#include <gtk/gtk.h>

void edens_navigation_setup(
    GtkWidget *file_area,
    GtkWidget *back_button,
    GtkWidget *forward_button,
    GtkWidget *path_entry,
    GtkWidget *status_label);

gboolean edens_navigation_go(GtkWidget *file_area, const char *path);

#endif
