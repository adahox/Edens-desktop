#ifndef FILEITEM_H
#define FILEITEM_H

#include <gtk/gtk.h>
#include "processors/directories.h"

/* Glass cards — holographic grid. */
#define EDENS_FILE_ICON_SIZE 72
#define EDENS_FILE_ITEM_WIDTH 128
#define EDENS_FILE_LABEL_MAX_LINES 2

GtkWidget *build_file_item(
    GtkWidget *file_area,
    EdensDirectoryEntry *entry
);

#endif
