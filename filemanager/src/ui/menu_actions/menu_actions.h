#ifndef MENU_ACTIONS_H
#define MENU_ACTIONS_H

#include <gtk/gtk.h>

void menu_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_files(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void menu_action_close(GSimpleAction *action, GVariant *parameter, gpointer user_data);
#endif