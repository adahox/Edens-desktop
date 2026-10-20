#ifndef EDENS_APP_ICON_H
#define EDENS_APP_ICON_H

#include <gtk/gtk.h>

#define EDENS_ICON_RESOURCE "/app/edens/file-icon-manager.png"
void edens_setup_application_icon(GtkApplication *app);
void edens_window_set_icon(GtkWidget *window);

#endif
