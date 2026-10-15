#include <gtk/gtk.h>
#include "toolbar.h"

GtkWidget *build_toolbar(void)
{
    GtkWidget *toolbar = gtk_box_new(
        GTK_ORIENTATION_HORIZONTAL,
        3
    );

    gtk_widget_set_name(toolbar, "explorer-toolbar");

    // botões de navegação
    GtkWidget *backButton = gtk_button_new_from_icon_name("go-previous", GTK_ICON_SIZE_BUTTON);
    GtkWidget *forwardButton = gtk_button_new_from_icon_name("go-next", GTK_ICON_SIZE_BUTTON);

    gtk_box_pack_start(GTK_BOX(toolbar),backButton,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(toolbar),forwardButton,FALSE,FALSE,0);


    GtkWidget *pathEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pathEntry), "Procurar...");

    gtk_widget_set_halign(pathEntry, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(toolbar),pathEntry,TRUE,TRUE,0);
    
    GtkWidget *btn_hidden = gtk_button_new_from_icon_name("eye-not-looking-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn_hidden, "Mostrar/Ocultar arquivos ocultos (Ctrl+H)");

    gtk_box_pack_start(GTK_BOX(toolbar),btn_hidden,FALSE,FALSE,0);
    return toolbar;
}
