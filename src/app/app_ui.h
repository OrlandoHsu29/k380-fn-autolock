#ifndef APP_UI_H
#define APP_UI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int app_ui_register(HINSTANCE instance);
void app_ui_unregister(HINSTANCE instance);
void app_ui_show_settings(void);
void app_ui_update(void);
void app_ui_set_tray_visibility(int visible);
void app_ui_on_taskbar_created(void);
void app_ui_handle_tray_callback(WPARAM wparam, LPARAM lparam);

#endif
