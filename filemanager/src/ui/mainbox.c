#include <gtk/gtk.h>
#include "mainbox.h"

GtkWidget *build_main_box(void)
{
    GtkWidget *main_box = gtk_box_new(
        GTK_ORIENTATION_VERTICAL,
        0
    );

    gtk_widget_set_name(main_box, "main-box");

    return main_box;
}
