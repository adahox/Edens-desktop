#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <gtk/gtk.h>

GtkWidget *build_toolbar(void);

GtkWidget *toolbar_get_back_button(GtkWidget *toolbar);
GtkWidget *toolbar_get_forward_button(GtkWidget *toolbar);
GtkWidget *toolbar_get_path_entry(GtkWidget *toolbar);

#endif
