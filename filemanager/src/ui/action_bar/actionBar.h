#ifndef ACTIONBAR_H
#define ACTIONBAR_H

#include <gtk/gtk.h>

GtkWidget *build_action_bar(void);
GtkWidget *action_bar_get_status_label(GtkWidget *action_bar);

#endif
