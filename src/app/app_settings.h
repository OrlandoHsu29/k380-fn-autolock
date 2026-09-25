#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

typedef struct app_preferences {
    int saved_fn_locked;
    int saved_show_tray_icon;
} app_preferences;

void app_settings_load(app_preferences *preferences);
int app_settings_store_fn_locked(int enabled);
int app_settings_store_tray_visible(int visible);
int app_settings_autostart_enabled(void);
int app_settings_set_autostart_enabled(int enabled);

#endif
