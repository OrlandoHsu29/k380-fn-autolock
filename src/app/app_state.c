#include "app_state.h"

app_state g_app = {
    .needs_apply = 1,
    .last_apply_result = 1,
    .fn_locked = 1,
    .show_tray_icon = 1
};
