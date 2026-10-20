#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "core/widget/events.h"
#include "ui/file_item/fileitem.h"
#include "ui/file_area/filearea.h"
#include "ui/navigation/navigation.h"

enum {
    FILE_ICON_SIZE = EDENS_FILE_ICON_SIZE,
    FILE_ITEM_WIDTH = EDENS_FILE_ITEM_WIDTH,
};

typedef struct {
    GtkWidget *file_area;
    char *name;
    EdensDirectoryEntryType type;
} FileItemActivateData;

typedef struct {
    GtkWidget *file_area;
    char *path;
} FileItemNavigateRequest;

static gboolean file_item_navigate_idle(gpointer user_data)
{
    FileItemNavigateRequest *request = user_data;

    edens_navigation_go(request->file_area, request->path);
    g_free(request->path);
    g_free(request);

    return G_SOURCE_REMOVE;
}

static void file_item_activate_data_free(gpointer data, GClosure *closure)
{
    (void)closure;
    FileItemActivateData *item = data;

    g_free(item->name);
    g_free(item);
}

static void configure_item_label(GtkWidget *label, int max_width_px)
{
    if (max_width_px < 1)
    {
        return;
    }

    gtk_widget_set_size_request(label, max_width_px, -1);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_CHAR);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_lines(GTK_LABEL(label), EDENS_FILE_LABEL_MAX_LINES);

    PangoLayout *layout = gtk_widget_create_pango_layout(label, "n");
    int char_width = 1;

    pango_layout_get_pixel_size(layout, &char_width, NULL);
    g_object_unref(layout);

    if (char_width < 1)
    {
        char_width = 1;
    }

    int max_chars = max_width_px / char_width;
    if (max_chars < 1)
    {
        max_chars = 1;
    }

    gtk_label_set_max_width_chars(GTK_LABEL(label), max_chars);
}

static gboolean file_item_pointer_crossing(
    GtkWidget *widget,
    const GdkEventCrossing *event,
    gpointer user_data)
{
    (void)event;
    gboolean entering = GPOINTER_TO_INT(user_data) != 0;

    if (entering) {
        gtk_widget_set_state_flags(widget, GTK_STATE_FLAG_PRELIGHT, FALSE);
    } else {
        gtk_widget_unset_state_flags(widget, GTK_STATE_FLAG_PRELIGHT);
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean open_folder(const GtkWidget *widget, const GdkEventButton *event, gpointer user_data)
{
    (void)widget;
    FileItemActivateData *item = user_data;

    if (event->type != GDK_DOUBLE_BUTTON_PRESS || event->button != 1)
    {
        return GDK_EVENT_PROPAGATE;
    }

    if (item->type != EDENS_ENTRY_DIRECTORY)
    {
        return GDK_EVENT_PROPAGATE;
    }

    const char *current = file_area_get_directory(item->file_area);

    if (current == NULL)
    {
        return GDK_EVENT_PROPAGATE;
    }

    FileItemNavigateRequest *request = g_new0(FileItemNavigateRequest, 1);

    request->file_area = item->file_area;
    request->path = g_build_filename(current, item->name, NULL);
    g_idle_add(file_item_navigate_idle, request);

    return GDK_EVENT_STOP;
}

GtkWidget *build_file_item(GtkWidget *file_area, EdensDirectoryEntry *entry)
{
    EWidget *shell = make(GTK_TYPE_EVENT_BOX, "file-item");
    EWidget *item  = make(GTK_TYPE_BOX, "file-item-content");

    add_prop(item, "orientation", GTK_ORIENTATION_VERTICAL);
    add_prop(item, "spacing", 8);
    add_prop(item, "halign", GTK_ALIGN_CENTER);

    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(shell->object)),
        entry->type == EDENS_ENTRY_DIRECTORY
            ? "file-item-directory"
            : "file-item-file");

    EWidget *file_icon = make(GTK_TYPE_IMAGE, "file-icon");
    add_prop(file_icon, "icon-name", "text-x-generic-symbolic");
    add_prop(file_icon, "icon-size", GTK_ICON_SIZE_DND);
    add_prop(file_icon, "pixel-size", FILE_ICON_SIZE);
    add_prop(file_icon, "halign", GTK_ALIGN_CENTER);

    if (entry->type == EDENS_ENTRY_DIRECTORY) {
        add_prop(file_icon, "icon-name", "folder-symbolic");
    }

    EWidget *file_label = make(GTK_TYPE_LABEL, "file-item-label");
    
    add_prop(file_label, "label", entry->name);
    configure_item_label(GTK_WIDGET(file_label->object), FILE_ITEM_WIDTH - 4);
    add_prop(file_label, "xalign", 0.5);
    add_prop(file_label, "halign", GTK_ALIGN_CENTER);
    add_prop(file_label, "justify", GTK_JUSTIFY_CENTER);

    gtk_box_pack_start(
        GTK_BOX(item->object),
        GTK_WIDGET(file_icon->object),
        FALSE,
        FALSE,
        0
    );
    gtk_box_pack_start(
        GTK_BOX(item->object),
        GTK_WIDGET(file_label->object),
        FALSE,
        FALSE,
        0
    );
    gtk_container_add(GTK_CONTAINER(shell->object), GTK_WIDGET(item->object));

    add_prop(shell, "width-request", FILE_ITEM_WIDTH);
    add_prop(shell, "hexpand", FALSE);
    add_prop(shell, "vexpand", FALSE);
    add_prop(shell, "halign", GTK_ALIGN_CENTER);
    add_prop(shell, "margin-top", 0);
    add_prop(shell, "margin-bottom", 0);
    add_prop(shell, "margin-start", 0);
    add_prop(shell, "margin-end", 0);

    add_event(
        shell,
        "enter-notify-event",
        G_CALLBACK(file_item_pointer_crossing),
        GINT_TO_POINTER(1),
        NULL
    );
    add_event(
        shell,
        "leave-notify-event",
        G_CALLBACK(file_item_pointer_crossing),
        GINT_TO_POINTER(0),
        NULL
    );

    FileItemActivateData *activate_data = g_new0(FileItemActivateData, 1);

    activate_data->file_area = file_area;
    activate_data->name = g_strdup(entry->name);
    activate_data->type = entry->type;

    add_event(
        shell,
        "button-press-event",
        G_CALLBACK(open_folder),
        activate_data,
        file_item_activate_data_free
    );
    
    return GTK_WIDGET(shell->object);
}
