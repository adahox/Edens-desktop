#include <errno.h>
#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/file_area/filearea.h"
#include "ui/file_item/fileitem.h"
#define FILE_AREA_PATH_KEY "edens-directory-path"
#define FILE_AREA_FLOW_KEY "edens-file-area-flow"
#define FILE_AREA_SYNC_WIDTH_KEY "edens-sync-width"
#define FILE_AREA_SYNC_HEIGHT_KEY "edens-sync-height"

static GtkWidget *file_area_find_surface(GtkWidget *file_area)
{
    for (GtkWidget *widget = gtk_widget_get_parent(file_area);
         widget != NULL;
         widget = gtk_widget_get_parent(widget))
    {
        if (file_area_surface_get_flow(widget) == file_area)
        {
            return widget;
        }
    }

    return NULL;
}

static GtkWidget *file_area_find_scrolled(GtkWidget *widget)
{
    for (GtkWidget *ancestor = widget;
         ancestor != NULL;
         ancestor = gtk_widget_get_parent(ancestor))
    {
        if (GTK_IS_SCROLLED_WINDOW(ancestor))
        {
            return ancestor;
        }
    }

    return NULL;
}

static GtkAllocation file_area_viewport_allocation(GtkWidget *scrolled)
{
    GtkAllocation viewport = {0, 0, 0, 0};
    GtkWidget *viewport_widget = gtk_bin_get_child(GTK_BIN(scrolled));

    if (viewport_widget != NULL)
    {
        gtk_widget_get_allocation(viewport_widget, &viewport);
    }

    if (viewport.width <= 0 || viewport.height <= 0)
    {
        gtk_widget_get_allocation(scrolled, &viewport);
    }

    return viewport;
}

/*
 * Filho do ScrolledWindow define a área scrollável via size-request.
 * Altura do conteúdo tem de ser medida com a largura do viewport (grelha).
 */
static void file_area_sync_scroll_layout(GtkWidget *scrolled, GtkWidget *surface)
{
    GtkAllocation viewport = file_area_viewport_allocation(scrolled);
    GtkWidget *file_area = file_area_surface_get_flow(surface);

    if (file_area == NULL || viewport.width <= 0 || viewport.height <= 0)
    {
        return;
    }

    gtk_widget_set_size_request(file_area, viewport.width, -1);

    gint content_min = 0;
    gint content_nat = 0;

    gtk_widget_get_preferred_height_for_width(
        file_area,
        viewport.width,
        &content_min,
        &content_nat
    );

    int surface_height = content_nat;

    if (viewport.height > surface_height)
    {
        surface_height = viewport.height;
    }

    int last_width = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(surface), FILE_AREA_SYNC_WIDTH_KEY)
    );
    int last_height = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(surface), FILE_AREA_SYNC_HEIGHT_KEY)
    );

    if (last_width != viewport.width || last_height != surface_height)
    {
        gtk_widget_set_size_request(surface, viewport.width, surface_height);
        g_object_set_data(
            G_OBJECT(surface),
            FILE_AREA_SYNC_WIDTH_KEY,
            GINT_TO_POINTER(viewport.width)
        );
        g_object_set_data(
            G_OBJECT(surface),
            FILE_AREA_SYNC_HEIGHT_KEY,
            GINT_TO_POINTER(surface_height)
        );
    }

    GtkPolicyType vertical_policy = GTK_POLICY_AUTOMATIC;

    if (content_nat <= viewport.height)
    {
        vertical_policy = GTK_POLICY_NEVER;
    }

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        vertical_policy
    );
}

static void file_area_on_scrolled_allocate(
    GtkWidget *scrolled,
    GtkAllocation *allocation,
    gpointer user_data
)
{
    (void)allocation;
    file_area_sync_scroll_layout(scrolled, GTK_WIDGET(user_data));
}

void file_area_bind_scrolled(GtkWidget *surface, GtkWidget *scrolled_window)
{
    g_signal_connect(
        scrolled_window,
        "size-allocate",
        G_CALLBACK(file_area_on_scrolled_allocate),
        surface
    );
}

GtkWidget *file_area_surface_get_flow(GtkWidget *surface)
{
    return g_object_get_data(G_OBJECT(surface), FILE_AREA_FLOW_KEY);
}

static GtkWidget *file_area_build_white_plate(void)
{
    GtkWidget *plate = gtk_event_box_new();

    gtk_event_box_set_visible_window(GTK_EVENT_BOX(plate), TRUE);
    gtk_widget_set_name(plate, "file-area-bg");
    gtk_widget_set_hexpand(plate, TRUE);
    gtk_widget_set_vexpand(plate, TRUE);
    gtk_widget_set_halign(plate, GTK_ALIGN_FILL);
    gtk_widget_set_valign(plate, GTK_ALIGN_FILL);

    return plate;
}

GtkWidget *build_file_area_surface(void)
{
    GtkWidget *overlay = gtk_overlay_new();

    gtk_widget_set_name(overlay, "file-area-surface");

    GtkWidget *plate = file_area_build_white_plate();

    gtk_container_add(GTK_CONTAINER(overlay), plate);

    GtkWidget *layer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_widget_set_name(layer, "file-area-layer");

    GtkWidget *file_area = build_file_area();

    g_object_set_data(G_OBJECT(overlay), FILE_AREA_FLOW_KEY, file_area);

    gtk_box_pack_start(GTK_BOX(layer), file_area, FALSE, FALSE, 0);
    gtk_widget_set_halign(layer, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(layer, TRUE);
    gtk_widget_set_valign(layer, GTK_ALIGN_START);
    gtk_widget_set_halign(file_area, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(file_area, TRUE);
    gtk_widget_set_vexpand(file_area, FALSE);
    gtk_widget_set_valign(file_area, GTK_ALIGN_START);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), layer);

    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, FALSE);
    gtk_widget_set_halign(overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(overlay, GTK_ALIGN_START);

    return overlay;
}

static void file_area_clear(GtkWidget *file_area)
{
    GtkFlowBox *box = GTK_FLOW_BOX(file_area);

    for (;;)
    {
        GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index(box, 0);

        if (child == NULL)
        {
            break;
        }

        gtk_widget_destroy(GTK_WIDGET(child));
    }
}

const char *file_area_get_directory(GtkWidget *file_area)
{
    return g_object_get_data(G_OBJECT(file_area), FILE_AREA_PATH_KEY);
}

int file_area_load_directory(GtkWidget *file_area, const char *path)
{
    if (file_area == NULL || path == NULL)
    {
        return EINVAL;
    }

    EdensDirectoryEntries entries;
    int error = read_directory(path, &entries);

    if (error != 0)
    {
        return error;
    }

    file_area_clear(file_area);

    g_object_set_data_full(
        G_OBJECT(file_area),
        FILE_AREA_PATH_KEY,
        g_strdup(path),
        g_free
    );

    populate_file_area(file_area, &entries);
    free_directory_entries(&entries);

    gtk_widget_show_all(file_area);

    GtkWidget *surface = file_area_find_surface(file_area);

    if (surface != NULL)
    {
        g_object_set_data(
            G_OBJECT(surface),
            FILE_AREA_SYNC_WIDTH_KEY,
            GINT_TO_POINTER(-1)
        );
        g_object_set_data(
            G_OBJECT(surface),
            FILE_AREA_SYNC_HEIGHT_KEY,
            GINT_TO_POINTER(-1)
        );

        GtkWidget *scrolled = file_area_find_scrolled(surface);

        if (scrolled != NULL)
        {
            file_area_sync_scroll_layout(scrolled, surface);
        }
        else
        {
            gtk_widget_queue_resize(surface);
        }
    }

    return 0;
}

GtkWidget *build_file_area(void)
{
    EWidget *file_area = make(GTK_TYPE_FLOW_BOX, "file-area");

    add_prop(file_area, "row-spacing", EDENS_FILE_AREA_ROW_SPACING);
    add_prop(file_area, "column-spacing", EDENS_FILE_AREA_COLUMN_SPACING);
    add_prop(file_area, "margin", EDENS_FILE_AREA_MARGIN);
    add_prop(file_area, "selection-mode", GTK_SELECTION_NONE);

    GtkWidget *flow = GTK_WIDGET(file_area->object);

    GtkFlowBox *box = GTK_FLOW_BOX(flow);

    gtk_flow_box_set_homogeneous(box, FALSE);
    gtk_flow_box_set_max_children_per_line(
        box,
        EDENS_FILE_AREA_MAX_CHILDREN_PER_LINE
    );
    gtk_flow_box_set_row_spacing(box, EDENS_FILE_AREA_ROW_SPACING);
    gtk_flow_box_set_column_spacing(box, EDENS_FILE_AREA_COLUMN_SPACING);

    gtk_widget_set_hexpand(flow, TRUE);
    gtk_widget_set_halign(flow, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(flow, FALSE);
    gtk_widget_set_valign(flow, GTK_ALIGN_START);

    return flow;
}

void populate_file_area(GtkWidget *file_area, EdensDirectoryEntries *entries)
{
    for (size_t i = 0; i < entries->count; i++)
    {
        GtkWidget *item = build_file_item(
            file_area,
            &entries->entries[i]
        );

        gtk_flow_box_insert(GTK_FLOW_BOX(file_area), item, -1);
    }
}
