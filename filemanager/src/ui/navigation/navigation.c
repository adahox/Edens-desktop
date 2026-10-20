#include "ui/navigation/navigation.h"

#include "ui/file_area/filearea.h"

#include <string.h>

#define NAV_KEY "edens-navigation"

typedef struct {
    GPtrArray *back;
    GPtrArray *forward;
    GtkWidget *file_area;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *path_entry;
    GtkWidget *status_label;
} EdensNavigation;

static void free_string_array(GPtrArray *array)
{
    if (array == NULL) {
        return;
    }

    g_ptr_array_free(array, TRUE);
}

static void navigation_free(gpointer data)
{
    EdensNavigation *nav = data;

    free_string_array(nav->back);
    free_string_array(nav->forward);
    g_free(nav);
}

static char *canonicalize_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    char *canonical = g_canonicalize_filename(path, NULL);

    if (canonical != NULL) {
        return canonical;
    }

    return g_strdup(path);
}

static void update_nav_buttons(EdensNavigation *nav)
{
    gtk_widget_set_sensitive(
        nav->back_button,
        nav->back != NULL && nav->back->len > 0);
    gtk_widget_set_sensitive(
        nav->forward_button,
        nav->forward != NULL && nav->forward->len > 0);
}

static void update_path_entry(EdensNavigation *nav, const char *path)
{
    if (nav->path_entry == NULL || path == NULL) {
        return;
    }

    gtk_entry_set_text(GTK_ENTRY(nav->path_entry), path);
}

static void update_status(EdensNavigation *nav)
{
    if (nav->status_label == NULL || nav->file_area == NULL) {
        return;
    }

    guint count = 0;
    GList *children = gtk_container_get_children(GTK_CONTAINER(nav->file_area));

    for (GList *node = children; node != NULL; node = node->next) {
        ++count;
    }

    g_list_free(children);

    char *text = g_strdup_printf(
        count == 1 ? "%u item" : "%u itens",
        count);
    gtk_label_set_text(GTK_LABEL(nav->status_label), text);
    g_free(text);
}

static void refresh_chrome(EdensNavigation *nav, const char *path)
{
    update_path_entry(nav, path);
    update_nav_buttons(nav);
    update_status(nav);
}

static void on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    EdensNavigation *nav = user_data;

    if (nav->back == NULL || nav->back->len == 0) {
        return;
    }

    const char *current = file_area_get_directory(nav->file_area);
    char *target = g_ptr_array_steal_index(nav->back, nav->back->len - 1);

    if (current != NULL) {
        g_ptr_array_add(nav->forward, g_strdup(current));
    }

    if (file_area_load_directory(nav->file_area, target) != 0) {
        g_ptr_array_add(nav->back, target);

        if (nav->forward->len > 0) {
            g_free(g_ptr_array_steal_index(nav->forward, nav->forward->len - 1));
        }

        update_nav_buttons(nav);
        return;
    }

    g_free(target);
    refresh_chrome(nav, file_area_get_directory(nav->file_area));
}

static void on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    EdensNavigation *nav = user_data;

    if (nav->forward == NULL || nav->forward->len == 0) {
        return;
    }

    const char *current = file_area_get_directory(nav->file_area);
    char *target = g_ptr_array_steal_index(nav->forward, nav->forward->len - 1);

    if (current != NULL) {
        g_ptr_array_add(nav->back, g_strdup(current));
    }

    if (file_area_load_directory(nav->file_area, target) != 0) {
        g_ptr_array_add(nav->forward, target);

        if (nav->back->len > 0) {
            g_free(g_ptr_array_steal_index(nav->back, nav->back->len - 1));
        }

        update_nav_buttons(nav);
        return;
    }

    g_free(target);
    refresh_chrome(nav, file_area_get_directory(nav->file_area));
}

static void on_path_activate(GtkEntry *entry, gpointer user_data)
{
    EdensNavigation *nav = user_data;
    const char *text = gtk_entry_get_text(entry);

    edens_navigation_go(nav->file_area, text);
}

void edens_navigation_setup(
    GtkWidget *file_area,
    GtkWidget *back_button,
    GtkWidget *forward_button,
    GtkWidget *path_entry,
    GtkWidget *status_label)
{
    if (file_area == NULL) {
        return;
    }

    EdensNavigation *nav = g_new0(EdensNavigation, 1);

    nav->back = g_ptr_array_new_with_free_func(g_free);
    nav->forward = g_ptr_array_new_with_free_func(g_free);
    nav->file_area = file_area;
    nav->back_button = back_button;
    nav->forward_button = forward_button;
    nav->path_entry = path_entry;
    nav->status_label = status_label;

    g_object_set_data_full(G_OBJECT(file_area), NAV_KEY, nav, navigation_free);

    if (back_button != NULL) {
        gtk_widget_set_sensitive(back_button, FALSE);
        g_signal_connect(back_button, "clicked", G_CALLBACK(on_back_clicked), nav);
    }

    if (forward_button != NULL) {
        gtk_widget_set_sensitive(forward_button, FALSE);
        g_signal_connect(
            forward_button,
            "clicked",
            G_CALLBACK(on_forward_clicked),
            nav);
    }

    if (path_entry != NULL) {
        g_signal_connect(
            path_entry,
            "activate",
            G_CALLBACK(on_path_activate),
            nav);
    }
}

gboolean edens_navigation_go(GtkWidget *file_area, const char *path)
{
    EdensNavigation *nav =
        g_object_get_data(G_OBJECT(file_area), NAV_KEY);
    char *canonical = canonicalize_path(path);

    if (canonical == NULL) {
        return FALSE;
    }

    const char *current =
        file_area != NULL ? file_area_get_directory(file_area) : NULL;

    if (current != NULL && strcmp(current, canonical) == 0) {
        g_free(canonical);
        if (nav != NULL) {
            refresh_chrome(nav, current);
        }
        return TRUE;
    }

    if (file_area_load_directory(file_area, canonical) != 0) {
        g_free(canonical);
        return FALSE;
    }

    if (nav != NULL) {
        if (current != NULL) {
            g_ptr_array_add(nav->back, g_strdup(current));
        }

        if (nav->forward != NULL && nav->forward->len > 0) {
            g_ptr_array_set_size(nav->forward, 0);
        }

        refresh_chrome(nav, file_area_get_directory(file_area));
    }

    g_free(canonical);
    return TRUE;
}
