#include "ui/app_icon/app_icon.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

static GdkPixbuf *load_icon_pixbuf(GError **error)
{
    return gdk_pixbuf_new_from_resource(EDENS_ICON_RESOURCE, error);
}

static GList *build_icon_list(GdkPixbuf *source)
{
    static const int sizes[] = {16, 32, 48, 64, 128, 256, 512, 0};
    GList *list = NULL;

    for (int i = 0; sizes[i] != 0; i++) {
        int size = sizes[i];
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
            source,
            size,
            size,
            GDK_INTERP_BILINEAR
        );
        if (scaled != NULL) {
            list = g_list_append(list, scaled);
        }
    }

    return list;
}

static void free_icon_list(GList *list)
{
    g_list_free_full(list, g_object_unref);
}

void edens_setup_application_icon(GtkApplication *app)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf = load_icon_pixbuf(&error);

    if (pixbuf == NULL) {
        g_warning("edens: failed to load app icon: %s", error->message);
        g_clear_error(&error);
        return;
    }

    (void)app;

    GList *icons = build_icon_list(pixbuf);
    if (icons != NULL) {
        gtk_window_set_default_icon_list(icons);
        free_icon_list(icons);
    }

    g_object_unref(pixbuf);
}

void edens_window_set_icon(GtkWidget *window)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf = load_icon_pixbuf(&error);

    if (pixbuf == NULL) {
        g_warning("edens: failed to load window icon: %s", error->message);
        g_clear_error(&error);
        return;
    }

    GList *icons = build_icon_list(pixbuf);
    if (icons != NULL) {
        gtk_window_set_icon_list(GTK_WINDOW(window), icons);
        free_icon_list(icons);
    } else {
        gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
    }

    g_object_unref(pixbuf);
}
