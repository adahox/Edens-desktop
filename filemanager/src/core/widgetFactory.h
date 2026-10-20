#ifndef WIDGET_FACTORY_H
#define WIDGET_FACTORY_H

#include <gtk/gtk.h>

typedef struct {
    char *id;
    GObject *object;
} EWidget;

EWidget *make(GType type, const char *name);
EWidget *make_pango_layout(GtkWidget *context, const char *name, const char *text);

#define add_prop(ew, prop, val) \
    g_object_set((ew)->object, (prop), (val), NULL)
#endif
