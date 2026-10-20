#include "ui/theme/theme.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

#define EDENS_CSS_RESOURCE "/app/edens/edens.css"

static GtkCssProvider *g_edens_css_provider;

typedef enum {
    EDENS_CSS_FROM_FILE,
    EDENS_CSS_FROM_RESOURCE,
    EDENS_CSS_FALLBACK
} EdensCssOrigin;

static char *resolve_css_path(void)
{
    const char *from_env = g_getenv("EDENS_DATA_DIR");

    if (from_env != NULL) {
        char *path = g_build_filename(from_env, "edens.css", NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            return path;
        }
        g_free(path);
    }

    char *dev_path = g_build_filename("data", "edens.css", NULL);
    if (g_file_test(dev_path, G_FILE_TEST_EXISTS)) {
        return dev_path;
    }
    g_free(dev_path);

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe != NULL) {
        char *dir = g_path_get_dirname(exe);
        char *path = g_build_filename(dir, "data", "edens.css", NULL);
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

    if (g_resources_lookup_data(
            EDENS_CSS_RESOURCE,
            G_RESOURCE_LOOKUP_FLAGS_NONE,
            NULL) != NULL) {
        gtk_css_provider_load_from_resource(provider, EDENS_CSS_RESOURCE);
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
