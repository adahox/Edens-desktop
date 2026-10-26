#include "ui/theme/theme.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define EDENS_CSS_RESOURCE "/app/edens/edens.css"
#define EDENS_CSS_DARK_RESOURCE "/app/edens/edens-dark.css"

static GtkCssProvider *g_edens_css_provider;

typedef enum {
    EDENS_CSS_FROM_FILE,
    EDENS_CSS_FROM_RESOURCE,
    EDENS_CSS_FALLBACK
} EdensCssOrigin;

static gboolean theme_is_dark(void)
{
    const char *home = g_get_home_dir();
    char *path = g_build_filename(home, ".config", "eden", "glass", NULL);
    gchar *contents = NULL;
    gboolean dark = FALSE;

    if (g_file_get_contents(path, &contents, NULL, NULL) && contents != NULL) {
        dark = strstr(contents, "dark=1") != NULL;
    }

    g_free(contents);
    g_free(path);
    return dark;
}

static const char *theme_css_name(void)
{
    return theme_is_dark() ? "edens-dark.css" : "edens.css";
}

static char *resolve_css_path(void)
{
    const char *css_name = theme_css_name();
    const char *from_env = g_getenv("EDENS_DATA_DIR");

    if (from_env != NULL) {
        char *path = g_build_filename(from_env, css_name, NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            return path;
        }
        g_free(path);
    }

    char *dev_path = g_build_filename("data", css_name, NULL);
    if (g_file_test(dev_path, G_FILE_TEST_EXISTS)) {
        return dev_path;
    }
    g_free(dev_path);

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe != NULL) {
        char *dir = g_path_get_dirname(exe);
        char *path = g_build_filename(dir, "data", css_name, NULL);
        g_free(dir);
        g_free(exe);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            return path;
        }
        g_free(path);
    }

    return NULL;
}

static GtkCssProvider *create_css_provider(EdensCssOrigin *origin_out, char **path_out)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GError *error = NULL;
    char *path = resolve_css_path();

    if (path != NULL && gtk_css_provider_load_from_path(provider, path, &error)) {
        *origin_out = EDENS_CSS_FROM_FILE;
        *path_out = path;
        return provider;
    }

    if (error != NULL) {
        fprintf(
            stderr,
            "edens: erro ao ler CSS '%s': %s\n",
            path != NULL ? path : "(sem caminho)",
            error->message
        );
        g_clear_error(&error);
    }
    g_free(path);

    const char *resource = theme_is_dark() ? EDENS_CSS_DARK_RESOURCE : EDENS_CSS_RESOURCE;

    if (g_resources_lookup_data(
            resource,
            G_RESOURCE_LOOKUP_FLAGS_NONE,
            NULL) != NULL) {
        gtk_css_provider_load_from_resource(provider, resource);
        *origin_out = EDENS_CSS_FROM_RESOURCE;
        *path_out = NULL;
        return provider;
    }

    fprintf(
        stderr,
        "edens: nenhum CSS encontrado. Janela magenta = tema NÃO carregou.\n"
    );
    gtk_css_provider_load_from_data(
        provider,
        "window { background-color: magenta; }",
        -1,
        NULL
    );
    *origin_out = EDENS_CSS_FALLBACK;
    *path_out = NULL;
    return provider;
}

void edens_apply_theme(GtkWidget *window)
{
    EdensCssOrigin origin;
    char *source_path = NULL;
    const char *source_label;

    if (g_edens_css_provider != NULL) {
        return;
    }

    g_edens_css_provider = create_css_provider(&origin, &source_path);

    GdkScreen *screen = gtk_widget_get_screen(window);
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(g_edens_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    switch (origin) {
    case EDENS_CSS_FROM_FILE:
        source_label = source_path;
        break;
    case EDENS_CSS_FROM_RESOURCE:
        source_label = "gresource:/app/edens/edens.css (rode make após editar data/edens.css)";
        break;
    case EDENS_CSS_FALLBACK:
    default:
        source_label = "fallback interno (magenta)";
        break;
    }

    fprintf(stderr, "edens: tema CSS ativo — fonte: %s\n", source_label);
    g_free(source_path);
}
