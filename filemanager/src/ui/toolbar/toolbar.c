#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/toolbar/toolbar.h"
#include "ui/icons/icons.h"

GtkWidget *build_toolbar(void)
{
    EWidget *toolbar = make(GTK_TYPE_BOX, "toolbar");

    add_prop(toolbar, "orientation", GTK_ORIENTATION_HORIZONTAL);
    add_prop(toolbar, "spacing", 8);

    GtkWidget *backButton = edens_icon_button_new(
        EDENS_ICON_GO_PREVIOUS,
        EDENS_ICON_GO_PREVIOUS_FALLBACK
    );
    GtkWidget *forwardButton = edens_icon_button_new(
        EDENS_ICON_GO_NEXT,
        EDENS_ICON_GO_NEXT_FALLBACK
    );

    gtk_box_pack_start(GTK_BOX(toolbar->object), backButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar->object), forwardButton, FALSE, FALSE, 0);

    EWidget *path_entry = make(GTK_TYPE_ENTRY, "path-entry");
    add_prop(path_entry, "placeholder-text", "Procurar...");
    add_prop(path_entry, "halign", GTK_ALIGN_FILL);

    gtk_box_pack_start(
        GTK_BOX(toolbar->object),
        GTK_WIDGET(path_entry->object),
        TRUE,
        TRUE,
        0
    );

    GtkWidget *btn_hidden = edens_icon_button_new(
        EDENS_ICON_EYE_HIDDEN,
        EDENS_ICON_EYE_HIDDEN_FALLBACK
    );
    gtk_widget_set_tooltip_text(
        btn_hidden,
        "Mostrar/Ocultar arquivos ocultos (Ctrl+H)"
    );

    gtk_box_pack_start(GTK_BOX(toolbar->object), btn_hidden, FALSE, FALSE, 0);

    return GTK_WIDGET(toolbar->object);
}
