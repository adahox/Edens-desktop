#ifndef MENU_H
#define MENU_H

#include <gtk/gtk.h>

void register_actions(GtkApplication *app);
GtkWidget *create_menu_button(GMenu *menu_model);
GMenu *create_menu(void);

#endif
