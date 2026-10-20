#include <gtk/gtk.h>

#include "core/widgetFactory.h"
#include "ui/fast_directories/fast_directories.h"
#include "ui/navigation/navigation.h"

typedef struct {
    const char *label;
    const char *icon;
    gboolean use_home;
    GUserDirectory directory;
} PlaceSpec;

static void on_place_clicked(GtkButton *button, gpointer user_data)
{
    GtkWidget *file_area = user_data;
    const char *path = g_object_get_data(G_OBJECT(button), "edens-place-path");

    if (path != NULL) {
        edens_navigation_go(file_area, path);
    }
}

static GtkWidget *make_place_button(
    const char *label,
    const char *icon_name,
    const char *path)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    GtkWidget *text = gtk_label_new(label);

    gtk_widget_set_name(button, "place-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "place-button");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(button, TRUE);

    gtk_label_set_xalign(GTK_LABEL(text), 0.0);
    gtk_widget_set_halign(text, GTK_ALIGN_START);
    gtk_widget_set_hexpand(text, TRUE);

    gtk_box_pack_start(GTK_BOX(row), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), row);

    g_object_set_data_full(G_OBJECT(button), "edens-place-path", g_strdup(path), g_free);

    return button;
}

GtkWidget *build_fast_directories(void)
{
    EWidget *sidebar = make(GTK_TYPE_BOX, "fast_directories");

    add_prop(sidebar, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(sidebar, "spacing", 4);

    GtkWidget *brand = gtk_label_new("eden");
    gtk_widget_set_name(brand, "sidebar-brand");
    gtk_label_set_xalign(GTK_LABEL(brand), 0.0);
    gtk_box_pack_start(GTK_BOX(sidebar->object), brand, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("seus lugares");
    gtk_widget_set_name(subtitle, "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
    gtk_box_pack_start(GTK_BOX(sidebar->object), subtitle, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(spacer, -1, 18);
    gtk_box_pack_start(GTK_BOX(sidebar->object), spacer, FALSE, FALSE, 0);

    return GTK_WIDGET(sidebar->object);
}

void fast_directories_bind(GtkWidget *sidebar, GtkWidget *file_area)
{
    if (sidebar == NULL || file_area == NULL) {
        return;
    }

    const PlaceSpec places[] = {
        {"Início", "user-home-symbolic", TRUE, G_USER_DIRECTORY_DESKTOP},
        {"Área de trabalho", "user-desktop-symbolic", FALSE, G_USER_DIRECTORY_DESKTOP},
        {"Documentos", "folder-documents-symbolic", FALSE, G_USER_DIRECTORY_DOCUMENTS},
        {"Downloads", "folder-download-symbolic", FALSE, G_USER_DIRECTORY_DOWNLOAD},
        {"Imagens", "folder-pictures-symbolic", FALSE, G_USER_DIRECTORY_PICTURES},
        {"Músicas", "folder-music-symbolic", FALSE, G_USER_DIRECTORY_MUSIC},
        {"Vídeos", "folder-videos-symbolic", FALSE, G_USER_DIRECTORY_VIDEOS},
    };

    for (guint index = 0; index < G_N_ELEMENTS(places); ++index) {
        const PlaceSpec *place = &places[index];
        const char *path = NULL;

        if (place->use_home) {
            path = g_get_home_dir();
        } else {
            path = g_get_user_special_dir(place->directory);
        }

        if (path == NULL) {
            continue;
        }

        GtkWidget *button = make_place_button(place->label, place->icon, path);

        g_signal_connect(button, "clicked", G_CALLBACK(on_place_clicked), file_area);
        gtk_box_pack_start(GTK_BOX(sidebar), button, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(sidebar);
}
