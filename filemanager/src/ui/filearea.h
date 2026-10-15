#ifndef FILEAREA_H
#define FILEAREA_H

#include <gtk/gtk.h>
#include "processors/directories.h"

GtkWidget *build_file_area(void);

void populate_file_area(
    GtkWidget *file_area,
    EdensDirectoryEntries *entries
);

#endif
