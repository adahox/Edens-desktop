#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/toolbar/toolbar.h"
#include "ui/icons/icons.h"

#define TOOLBAR_BACK_KEY "edens-toolbar-back"
#define TOOLBAR_FORWARD_KEY "edens-toolbar-forward"
#define TOOLBAR_PATH_KEY "edens-toolbar-path"

GtkWidget *build_toolbar(void)
{
    EWidget *toolbar = make(GTK_TYPE_BOX, "toolbar");

    add_prop(toolbar, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(toolbar, "spacing", 14);

    GtkWidget *heading = gtk_label_new("Explorar");
    gtk_widget_set_name(heading, "stage-heading");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_box_pack_start(GTK_BOX(toolbar->object), heading, FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(row, "toolbar-row");

    GtkWidget *nav_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(nav_group, "toolbar-nav");

    GtkWidget *backButton = edens_icon_button_new(
        EDENS_ICON_GO_PREVIOUS,
        EDENS_ICON_GO_PREVIOUS_FALLBACK
    );
    GtkWidget *forwardButton = edens_icon_button_new(
        EDENS_ICON_GO_NEXT,
        EDENS_ICON_GO_NEXT_FALLBACK
    );

    gtk_widget_set_tooltip_text(backButton, "Voltar");
    gtk_widget_set_tooltip_text(forwardButton, "Avançar");
    gtk_style_context_add_class(gtk_widget_get_style_context(backButton), "nav-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(forwardButton), "nav-button");

    gtk_box_pack_start(GTK_BOX(nav_group), backButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_group), forwardButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), nav_group, FALSE, FALSE, 0);

    EWidget *path_entry = make(GTK_TYPE_ENTRY, "path-entry");
    add_prop(path_entry, "placeholder-text", "Para onde?");
    add_prop(path_entry, "halign", GTK_ALIGN_FILL);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(path_entry->object)),
        "path-entry");

    gtk_box_pack_start(
        GTK_BOX(row),
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
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_hidden), "ghost-button");
    gtk_box_pack_start(GTK_BOX(row), btn_hidden, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(toolbar->object), row, FALSE, FALSE, 0);

    GtkWidget *root = GTK_WIDGET(toolbar->object);

    g_object_set_data(G_OBJECT(root), TOOLBAR_BACK_KEY, backButton);
    g_object_set_data(G_OBJECT(root), TOOLBAR_FORWARD_KEY, forwardButton);
    g_object_set_data(G_OBJECT(root), TOOLBAR_PATH_KEY, path_entry->object);

    return root;
}

GtkWidget *toolbar_get_back_button(GtkWidget *toolbar)
{
    return g_object_get_data(G_OBJECT(toolbar), TOOLBAR_BACK_KEY);
}

GtkWidget *toolbar_get_forward_button(GtkWidget *toolbar)
{
    return g_object_get_data(G_OBJECT(toolbar), TOOLBAR_FORWARD_KEY);
}

GtkWidget *toolbar_get_path_entry(GtkWidget *toolbar)
{
    return g_object_get_data(G_OBJECT(toolbar), TOOLBAR_PATH_KEY);
}
