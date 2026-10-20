#include <gtk/gtk.h>
#include "core/widgetFactory.h"
#include "ui/icons/icons.h"

static const char *pick_icon_name(const char *primary, const char *fallback)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();

    if (gtk_icon_theme_has_icon(theme, primary)) {
        return primary;
    }
    if (fallback != NULL && gtk_icon_theme_has_icon(theme, fallback)) {
        return fallback;
    }
    return primary;
}

GtkWidget *edens_icon_button_new(const char *primary, const char *fallback)
{
    const char *icon_name = pick_icon_name(primary, fallback);

    EWidget *button = make(GTK_TYPE_BUTTON, "icon-button");
    EWidget *image = make(GTK_TYPE_IMAGE, "icon-button-image");

    add_prop(image, "icon-name", icon_name);
    add_prop(image, "icon-size", GTK_ICON_SIZE_BUTTON);

    gtk_button_set_image(
        GTK_BUTTON(button->object),
        GTK_WIDGET(image->object)
    );

    return GTK_WIDGET(button->object);
}
