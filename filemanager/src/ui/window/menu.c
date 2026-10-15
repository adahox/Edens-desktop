#include "menu.h"
#include "menu_actions.h"

typedef struct MenuAction {
    char *label;
    char *icon;
    char *menu_name;
    char *detailed_name;
    void (*callback)(GSimpleAction *action, GVariant *parameter, gpointer user_data);
    char *hotkey;
    struct MenuAction *sub_menu;
} MenuAction;

static MenuAction file_menu[] = {
    {"Novo", "document-new", "new", "app.new", menu_action_new, "<Control>n", NULL},
    {"Abrir", "document-open", "open", "app.open", menu_action_open, "<Control>o", NULL},
    {"Salvar", "document-save", "save", "app.save", menu_action_save, "<Control>s", NULL},
    {"Salvar Como", "document-save-as", "save-as", "app.save-as", menu_action_save_as, "<Control>Shift+S", NULL},
    {"Fechar",   "window-close", "close", "app.close", menu_action_close, "<Control>w", NULL},
    {NULL,  NULL, NULL, NULL, NULL, NULL, NULL},
};

MenuAction menu_actions[] = {
    {"Arquivos", NULL, "files", "app.files", menu_action_files, "<Control>f", file_menu},
    {"Sobre",  NULL, "about", "app.about", menu_action_about, "<Control>a", NULL},
};

void set_hotkey(GtkApplication *app, char *menu_name, char *hotkey)
{
    gchar detailed_name[64];

    g_snprintf(detailed_name, sizeof(detailed_name), "app.%s", menu_name);
    const char *accels[] = {hotkey, NULL};
    gtk_application_set_accels_for_action(app, detailed_name, accels);
}

static size_t menu_length(MenuAction *actions)
{
    size_t count = 0;

    while (actions[count].label != NULL) {
        count++;
    }

    return count;
}

static void register_action_list(GtkApplication *app, MenuAction *actions, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        GSimpleAction *action = g_simple_action_new(actions[i].menu_name, NULL);

        g_signal_connect(action, "activate", G_CALLBACK(actions[i].callback), NULL);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
        g_object_unref(action);

        if (actions[i].hotkey != NULL) {
            set_hotkey(app, actions[i].menu_name, actions[i].hotkey);
        }

        if (actions[i].sub_menu != NULL) {
            register_action_list(app, actions[i].sub_menu, menu_length(actions[i].sub_menu));
        }
    }
}

void register_actions(GtkApplication *app)
{
    register_action_list(app, menu_actions, sizeof(menu_actions) / sizeof(menu_actions[0]));
}

GtkWidget *create_menu_button(GMenu *menu_model)
{
    GtkWidget *menu_button = gtk_menu_button_new();
    GtkWidget *menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);

    gtk_button_set_image(GTK_BUTTON(menu_button), menu_icon);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu_model));

    return menu_button;
}

static void addIcon(GMenuItem *item, const char *iconName)
{
    if (iconName == NULL) {
        return;
    }

    GIcon *icon = g_themed_icon_new(iconName);
    g_menu_item_set_icon(item, icon);
    g_object_unref(icon);
}   

static GMenu *build_menu(MenuAction *actions, size_t count)
{
    GMenu *menu = g_menu_new(); 

    for (size_t i = 0; i < count; i++) {

        const char *action = actions[i].sub_menu != NULL ? NULL : actions[i].detailed_name;
        GMenuItem *item = g_menu_item_new(actions[i].label, action);

        addIcon(item, actions[i].icon);

        if (actions[i].sub_menu != NULL) {
            GMenu *sub_menu = build_menu(actions[i].sub_menu, menu_length(actions[i].sub_menu));
            g_menu_item_set_submenu(item, G_MENU_MODEL(sub_menu));
            g_object_unref(sub_menu);
        }

        g_menu_append_item(menu, item);
        g_object_unref(item);
    }

    return menu;
}

GMenu *create_menu(void)
{
    return build_menu(menu_actions, sizeof(menu_actions) / sizeof(menu_actions[0]));
}

