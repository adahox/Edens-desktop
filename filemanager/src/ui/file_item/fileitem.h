#ifndef FILEITEM_H
#define FILEITEM_H

#include <gtk/gtk.h>
#include "processors/directories.h"

/* Grelha ampla no sheet flutuante. */
#define EDENS_FILE_ICON_SIZE 64
#define EDENS_FILE_ITEM_WIDTH 108
#define EDENS_FILE_LABEL_MAX_LINES 2

GtkWidget *build_file_item(
    GtkWidget *file_area,
    EdensDirectoryEntry *entry
);

#endif
