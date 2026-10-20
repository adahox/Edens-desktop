#include <gtk/gtk.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "ui/window/window.h"
#include "ui/main_box/mainbox.h"
#include "ui/toolbar/toolbar.h"
#include "ui/action_bar/actionBar.h"
#include "ui/file_area/filearea.h"
#include "ui/scrolled_window/scrolledwindow.h"
#include "ui/fast_directories/fast_directories.h"
#include "ui/content/content.h"
#include "ui/explorer_pane/explorer_pane.h"
#include "ui/app_icon/app_icon.h"
#include "ui/theme/theme.h"
#include "ui/navigation/navigation.h"

static void activate(GtkApplication *app, gpointer user_data)
{
    const char *start_path = user_data;

    if (start_path == NULL) {
        start_path = getenv("HOME");

        if (start_path == NULL) {
            start_path = ".";
        }
    }

    edens_setup_application_icon(app);

    GtkWidget *window = build_window(app);

    GtkWidget *root = build_main_box();
    GtkWidget *toolbar = build_toolbar();
    GtkWidget *file_surface = build_file_area_surface();
    GtkWidget *file_area = file_area_surface_get_flow(file_surface);
    GtkWidget *scrolled_window = build_scrolled_window();
    GtkWidget *sidebar = build_fast_directories();
    GtkWidget *action_bar = build_action_bar();

    gtk_container_add(GTK_CONTAINER(scrolled_window), file_surface);
    file_area_bind_scrolled(file_surface, scrolled_window);

    GtkWidget *content = build_content(scrolled_window);

    GtkWidget *stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(stage, "stage");
    gtk_box_pack_start(GTK_BOX(stage), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stage), content, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stage), action_bar, FALSE, FALSE, 0);

    GtkWidget *paned = build_explorer_pane(sidebar, stage);
    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), root);

    edens_navigation_setup(
        file_area,
        toolbar_get_back_button(toolbar),
        toolbar_get_forward_button(toolbar),
        toolbar_get_path_entry(toolbar),
        action_bar_get_status_label(action_bar));
    fast_directories_bind(sidebar, file_area);

    edens_apply_theme(window);

    if (!edens_navigation_go(file_area, start_path)) {
        fprintf(stderr, "Erro ao ler diretório '%s'\n", start_path);
    }

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[])
{
    const char *start_path = NULL;

    if (argc > 1) {
        start_path = argv[1];
    } else {
        start_path = getenv("HOME");

        if (start_path == NULL) {
            start_path = ".";
        }
    }

    GtkApplication *app = gtk_application_new(
        "com.edens.FileManager",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(
        app,
        "activate",
        G_CALLBACK(activate),
        (gpointer) start_path
    );

    int app_argc = 1;

    int status = g_application_run(
        G_APPLICATION(app),
        app_argc,
        argv
    );

    g_object_unref(app);

    return status;
}
