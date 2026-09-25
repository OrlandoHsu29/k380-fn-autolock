#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

void app_runtime_set_mode(int desired_fn_locked);
void app_runtime_set_tray_visibility(int visible);
void app_runtime_restart_in_background(void);

#endif
