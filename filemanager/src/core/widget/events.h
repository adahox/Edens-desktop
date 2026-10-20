#ifndef WIDGET_EVENTS_H
#define WIDGET_EVENTS_H

#include "core/widgetFactory.h"
#include <gtk/gtk.h>

void add_event(
    EWidget *widget,
    const char *signal,
    GCallback callback,
    gpointer user_data,
    GClosureNotify destroy_data
);

void edens_user_data_free(gpointer data, GClosure *closure);

#endif