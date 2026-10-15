#include <gtk/gtk.h>
#include "fileitem.h"

enum { FILE_ICON_SIZE = 64 };

static int label_width_chars(GtkWidget *label)
{
    PangoLayout *layout = gtk_widget_create_pango_layout(label, "n");
    int char_width = 1;

    pango_layout_get_pixel_size(layout, &char_width, NULL);
    g_object_unref(layout);

    if (char_width < 1) {
        char_width = 1;
    }

    return FILE_ICON_SIZE / char_width;
}

GtkWidget *build_file_item(EdensDirectoryEntry *entry)
{
    GtkWidget *item = gtk_box_new(
        GTK_ORIENTATION_VERTICAL,
        0
    );

    GtkWidget *file_icon;

    if (entry->type == EDENS_ENTRY_DIRECTORY) {
        file_icon = gtk_image_new_from_icon_name(
            "folder",
            GTK_ICON_SIZE_BUTTON
        );
    } else {
        file_icon = gtk_image_new_from_icon_name(
            "text-x-generic",
            GTK_ICON_SIZE_BUTTON
        );
    }

    gtk_image_set_pixel_size(GTK_IMAGE(file_icon), FILE_ICON_SIZE);

    GtkWidget *file_label = gtk_label_new(entry->name);

    gtk_widget_set_name(file_label, "file-item-label");
    
    int width_chars = label_width_chars(file_label);

    gtk_label_set_max_width_chars(GTK_LABEL(file_label), width_chars);
    gtk_label_set_ellipsize(GTK_LABEL(file_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(file_label), 0.5);
    gtk_widget_set_halign(file_label, GTK_ALIGN_CENTER);

    gtk_box_pack_start(
        GTK_BOX(item),
        file_icon,
        FALSE,
        FALSE,
        0
    );

    gtk_box_pack_start(
        GTK_BOX(item),
        file_label,
        FALSE,
        FALSE,
        0
    );
    

    return item;
}
