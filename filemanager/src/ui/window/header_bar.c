#include <gtk/gtk.h>

void set_header_bar_style(const char *hexColor) 
{
    GtkCssProvider *provider = gtk_css_provider_new();

    gchar *css = g_strdup_printf(
        /* Estilo da Headerbar */
        "headerbar {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: #ffffff;"
        "   border-width: 1px;"
        "   border-color:rgb(65, 66, 71);"
        "   box-shadow: none;"
        "}"
        "#explorer-toolbar {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: #ffffff;"
        "   border-width: 1px;"
        "   border-color:rgb(65, 66, 71);"
        "   box-shadow: none;"
        "   margin: 0;"
        "   padding: 4px 6px;"
        "   padding-top: 8px;"
        "   padding-bottom: 8px;"
        "}"
        "#explorer-toolbar button {"
        "   background-color:rgb(65, 66, 71);"
        "   background-image: none;"
        "   border-width: 0;"
        "   box-shadow: none;"
        "   padding: 4px;"
        "}"
        "#explorer-toolbar button:hover {"
        "   background-color: rgba(65, 66, 71, 0.55);"
        "}"
        "#scrolledwindow {"
        "   background-color: rgb(65, 66, 71);"
        "   border-width: 0;"
        "}"
        "#explorer-toolbar entry {"
        "   background-color:rgb(65, 66, 71);"
        "   background-image: none;"
        "   border-width: 0;"
        "   box-shadow: none;"
        "   padding: 4px;"
        "   color: #ffffff;"
        "}"
        "#file-item-label {"
        "   color: #ffffff;"
        "}"
        "#action-bar {"
        "   background-color: #252630;"
        "   color: #ffffff;"
        "   min-height: 32px;"
        "}"
        "#main-box {"
        "   background-color: rgb(65, 66, 71);"
        "   border-width: 0;"
        "}"
        "headerbar, titlebar {"
        "   box-shadow: none;"
        "   border-width: 1px;"
        "   border-color:rgb(65, 66, 71);"
        "}"
        /* Estilo da Barra de Menu Principal (MenuBar Superior) */
        "menubar {"
        "   background-color: %s;"
        "   color: #ffffff;"
        "   border-width: 1px;"
        "   border-color:rgb(65, 66, 71);"
        "   box-shadow: none;"
        "}"
        "menubar > menuitem {"
        "   color: #ffffff;"
        "}"
        "menubar > menuitem:hover {"
        "   background-color: #3e4059;"
        "}"
        /* Estilo dos Popups / Submenus */
        "popovercontents, popover, menu {"
        "   background-color: #252630;"
        "   color: #ffffff;"
        "   border: 1px solid #3b3d52;"
        "}"
        /* Destaque ao passar o rato nos itens */
        "popover modelbutton:hover, menuitem:hover {"
        "   background-color: #3e4059;"
        "   color: #ffffff;"
        "}", 
        hexColor, hexColor, hexColor);
    gtk_css_provider_load_from_data(provider, css, -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    g_free(css);
    g_object_unref(provider);

}


GtkWidget *create_header_bar()
{
    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Eden's Explorer");
    set_header_bar_style("#2E2D2D");
    
    return header_bar;
}