#ifndef FAST_DIRECTORIES_H
#define FAST_DIRECTORIES_H

#include <gtk/gtk.h>

GtkWidget *build_fast_directories(void);
void fast_directories_bind(GtkWidget *sidebar, GtkWidget *file_area);

#endif
