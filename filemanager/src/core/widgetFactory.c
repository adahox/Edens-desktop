#include <gtk/gtk.h>
#include <stdlib.h>
#include "core/widgetFactory.h"

static void ewidget_free(gpointer data)
{
    EWidget *ew = data;

    g_free(ew->id);
    g_free(ew);
}

static EWidget *ewidget_wrap(GObject *object, const char *name)
{
    if (object == NULL)
        return NULL;

    EWidget *ew = g_new0(EWidget, 1);

    ew->id = g_strdup(name);
    ew->object = object;

    if (GTK_IS_WIDGET(object) && name != NULL)
        gtk_widget_set_name(GTK_WIDGET(object), name);

    g_object_set_data_full(
        object,
        "edens-ewidget",
        ew,
        ewidget_free
    );

    return ew;
}

EWidget *make_pango_layout(GtkWidget *context, const char *name, const char *text)
{
    g_return_val_if_fail(GTK_IS_WIDGET(context), NULL);

    PangoLayout *layout =
        gtk_widget_create_pango_layout(context, text);

    if (layout == NULL)
        return NULL;

    return ewidget_wrap(G_OBJECT(layout), name);
}

EWidget *make(GType type, const char *name)
{
    if (!g_type_is_a(type, GTK_TYPE_WIDGET))
        g_error("Tipo inválido: esperado um GtkWidget");

    GObject *object = g_object_new(type, NULL);

    return ewidget_wrap(object, name);
}
