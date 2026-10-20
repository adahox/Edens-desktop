#include "ui/menu_actions/menu_actions.h"

void menu_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}


void menu_action_files(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}

void menu_action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}

void menu_action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}

void menu_action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}

void menu_action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}

void menu_action_close(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)parameter;
    (void)user_data;

    g_print ("Action: %s\n", g_action_get_name(G_ACTION(action)));
}