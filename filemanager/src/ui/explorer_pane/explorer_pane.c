#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/explorer_pane/explorer_pane.h"

enum { EXPLORER_PANE_SIDEBAR_WIDTH = 248 };

GtkWidget *build_explorer_pane(GtkWidget *sidebar, GtkWidget *content)
{
    EWidget *paned = make(GTK_TYPE_PANED, "explorer-paned");

    add_prop(paned, "orientation", GTK_ORIENTATION_HORIZONTAL);

    gtk_paned_pack1(GTK_PANED(paned->object), sidebar, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned->object), content, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(paned->object), EXPLORER_PANE_SIDEBAR_WIDTH);

    return GTK_WIDGET(paned->object);
}
