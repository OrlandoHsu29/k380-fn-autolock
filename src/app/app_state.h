#ifndef APP_STATE_H
#define APP_STATE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct app_state {
    int reconnect_retry;
    DWORD reconnect_started;
    int fn_locked;
    int show_tray_icon;
    int tray_icon_added;
    int last_apply_result;
    int autostart_enabled;
    HINSTANCE app_instance;
    HWND settings_window;
    HWND startup_checkbox;
    HWND fn_lock_radio;
    HWND fn_unlock_radio;
    HWND tray_checkbox;
    HWND status_label;
    UINT taskbar_created_message;
    HFONT settings_font;
    int owns_settings_font;
    HWND watcher_window;
    HICON fn_tray_icon;
} app_state;

extern app_state g_app;

#define APP_MAIN_WINDOW_CLASS L"K380FnAutoLockWindow"
#define WM_APP_SHOW_SETTINGS (WM_APP + 1)
#define WM_APP_TRAY_CALLBACK (WM_APP + 2)
#define TRAY_ICON_ID 1

#endif
