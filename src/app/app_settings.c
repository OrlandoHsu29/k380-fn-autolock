#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "app_settings.h"

#define REGISTRY_RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REGISTRY_PREFS_KEY L"Software\\K380FnAutoLock"
#define REGISTRY_RUN_VALUE L"K380FnAutoLock"
#define REGISTRY_FN_LOCKED L"FnLocked"
#define REGISTRY_SHOW_TRAY L"ShowTrayIcon"

static DWORD read_preference(const wchar_t *name, DWORD fallback)
{
    HKEY key;
    DWORD value = fallback;
    DWORD type = REG_DWORD;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PREFS_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return fallback;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value))
        value = fallback;
    RegCloseKey(key);
    return value;
}

static int write_preference(const wchar_t *name, DWORD value)
{
    HKEY key;
    LONG result;

    result = RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_PREFS_KEY, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void app_settings_load(app_preferences *preferences)
{
    if (preferences == NULL)
        return;
    preferences->saved_fn_locked = read_preference(REGISTRY_FN_LOCKED, 1) != 0;
    preferences->saved_show_tray_icon = read_preference(REGISTRY_SHOW_TRAY, 1) != 0;
}

int app_settings_store_fn_locked(int enabled)
{
    return write_preference(REGISTRY_FN_LOCKED, enabled != 0);
}

int app_settings_store_tray_visible(int visible)
{
    return write_preference(REGISTRY_SHOW_TRAY, visible != 0);
}

int app_settings_autostart_enabled(void)
{
    HKEY key;
    LONG result;
    DWORD size = 0;

    result = RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegQueryValueExW(key, REGISTRY_RUN_VALUE, NULL, NULL, NULL, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && size != 0;
}

int app_settings_set_autostart_enabled(int enabled)
{
    HKEY key;
    LONG result;

    if (!enabled) {
        result = RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND)
            return 1;
        if (result != ERROR_SUCCESS)
            return 0;
        result = RegDeleteValueW(key, REGISTRY_RUN_VALUE);
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    {
        wchar_t executable[MAX_PATH];
        wchar_t command[MAX_PATH + 16];
        DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
        DWORD bytes;

        if (length == 0 || length >= MAX_PATH)
            return 0;
        command[0] = L'"';
        memcpy(command + 1, executable, length * sizeof(wchar_t));
        command[length + 1] = L'"';
        command[length + 2] = L' ';
        memcpy(command + length + 3, L"--background", sizeof(L"--background"));
        bytes = (length + 3) * sizeof(wchar_t) + sizeof(L"--background");

        result = RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &key, NULL);
        if (result != ERROR_SUCCESS)
            return 0;
        result = RegSetValueExW(key, REGISTRY_RUN_VALUE, 0, REG_SZ, (const BYTE *)command, bytes);
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
}
