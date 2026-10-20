#ifndef FILEAREA_H
#define FILEAREA_H

#include <gtk/gtk.h>
#include "processors/directories.h"

/* Espaçamento da grelha (GtkFlowBox) — ajuste aqui. */
#define EDENS_FILE_AREA_ROW_SPACING 4
#define EDENS_FILE_AREA_COLUMN_SPACING 6
#define EDENS_FILE_AREA_MARGIN 0
/* GTK exige > 0; default 7 estica colunas. Valor alto ≈ sem limite por linha. */
#define EDENS_FILE_AREA_MAX_CHILDREN_PER_LINE 4096

GtkWidget *build_file_area(void);

/* Overlay: placa branca (#file-area-bg) + grelha (#file-area) por cima. */
GtkWidget *build_file_area_surface(void);

GtkWidget *file_area_surface_get_flow(GtkWidget *surface);

/* Ajusta largura/altura mínima do overlay quando o scroll redimensiona. */
void file_area_bind_scrolled(GtkWidget *surface, GtkWidget *scrolled_window);

const char *file_area_get_directory(GtkWidget *file_area);

int file_area_load_directory(GtkWidget *file_area, const char *path);

void populate_file_area(
    GtkWidget *file_area,
    EdensDirectoryEntries *entries
);

#endif
