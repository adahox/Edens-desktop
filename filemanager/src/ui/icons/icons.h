#ifndef EDENS_ICONS_H
#define EDENS_ICONS_H

#include <gtk/gtk.h>

#define EDENS_ICON_GO_PREVIOUS "go-previous-symbolic"
#define EDENS_ICON_GO_PREVIOUS_FALLBACK "go-previous"
#define EDENS_ICON_GO_NEXT "go-next-symbolic"
#define EDENS_ICON_GO_NEXT_FALLBACK "go-next"
#define EDENS_ICON_EYE_HIDDEN "eye-not-looking-symbolic"
#define EDENS_ICON_EYE_HIDDEN_FALLBACK "view-conceal-symbolic"

GtkWidget *edens_icon_button_new(const char *primary, const char *fallback);

#endif
