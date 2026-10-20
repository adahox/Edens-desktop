#include "core/widgetFactory.h"

void add_event(
    EWidget *widget,
    const char *signal,
    GCallback callback,
    gpointer user_data,
    GClosureNotify destroy_data
)
{
    if (widget == NULL || signal == NULL || callback == NULL)
    {
        g_warning(
            "add_event: argumento NULL (widget=%p, signal=%p, callback=%p)",
            (void *)widget,
            (void *)signal,
            (void *)callback
        );
        return;
    }

    g_signal_connect_data(
        GTK_WIDGET(widget->object),
        signal,
        callback,
        user_data,
        destroy_data,
        0
    );
}

void edens_user_data_free(gpointer data, const GClosure *closure)
{
    (void)closure;
    g_free(data);
}
