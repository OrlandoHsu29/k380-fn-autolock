#include <stdio.h>
#include <hidapi.h>

#include "k380_hid.h"

#ifdef AUTO_WATCH
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>
#include <bluetoothapis.h>
#include <stddef.h>
#include <string.h>

#include "app_state.h"
#include "app_settings.h"
#include "app_ui.h"

#define IDI_APP_ICON 101
#define RETRY_TIMER 1
#define RETRY_INTERVAL_MS 2000
#define REFRESH_INTERVAL_MS 60000
#define RECONNECT_RETRY_MS 12000
#define MAX_RADIO_WATCHES 8

typedef struct radio_watch {
    HANDLE handle;
    HDEVNOTIFY notification;
} radio_watch;

/* GUID_DEVINTERFACE_HID, used for HID interface arrival/removal notifications. */
static const GUID hid_interface_guid = {
    0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}
};
/* GUID_BLUETOOTH_HCI_EVENT reports ACL connection changes for a radio. */
static const GUID bluetooth_hci_event_guid = {
    0xfc240062, 0x1541, 0x49be, {0xb4, 0x63, 0x84, 0xc4, 0xdc, 0xd7, 0xbf, 0x7f}
};

static size_t watch_bluetooth_radios(HWND window, radio_watch *radios, size_t capacity)
{
    BLUETOOTH_FIND_RADIO_PARAMS params = {sizeof(params)};
    HBLUETOOTH_RADIO_FIND search;
    HANDLE radio;
    size_t count = 0;

    search = BluetoothFindFirstRadio(&params, &radio);
    if (search == NULL)
        return 0;

    do {
        DEV_BROADCAST_HANDLE filter = {0};
        HDEVNOTIFY notification;

        if (count == capacity) {
            CloseHandle(radio);
            continue;
        }

        filter.dbch_size = sizeof(filter);
        filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
        filter.dbch_handle = radio;
        notification = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (notification == NULL) {
            CloseHandle(radio);
            continue;
        }

        radios[count].handle = radio;
        radios[count].notification = notification;
        ++count;
    } while (BluetoothFindNextRadio(search, &radio));

    BluetoothFindRadioClose(search);
    return count;
}

static void try_apply(void)
{
    k380_key_mode mode = g_app.fn_locked ? K380_MODE_FN_LOCKED : K380_MODE_MEDIA_KEYS;

    g_app.last_apply_result = k380_apply_key_mode(mode, 1);
    if (g_app.last_apply_result == K380_APPLY_OK) {
        g_app.needs_apply = 0;
        g_app.last_success = GetTickCount();
        OutputDebugStringA(g_app.fn_locked ? "K380 Fn mode applied\n" : "K380 media mode applied\n");
    }
    app_ui_update();
}

void app_runtime_set_mode(int desired_fn_locked)
{
    g_app.fn_locked = desired_fn_locked != 0;
    if (!app_settings_store_fn_locked(g_app.fn_locked))
        MessageBoxW(g_app.settings_window, L"Fn 模式无法保存；本次运行仍会按当前选择处理。",
                    L"K380 Fn 设置", MB_OK | MB_ICONERROR);
    g_app.needs_apply = 1;
    g_app.last_apply_result = k380_apply_key_mode(g_app.fn_locked ? K380_MODE_FN_LOCKED : K380_MODE_MEDIA_KEYS, 1);
    if (g_app.last_apply_result == K380_APPLY_OK) {
        g_app.needs_apply = 0;
        g_app.last_success = GetTickCount();
    }
    app_ui_update();
}

void app_runtime_set_tray_visibility(int visible)
{
    g_app.show_tray_icon = visible != 0;
    if (!app_settings_store_tray_visible(g_app.show_tray_icon))
        MessageBoxW(g_app.settings_window, L"图标显示设置无法保存；本次运行仍会按当前选择处理。",
                    L"K380 Fn 设置", MB_OK | MB_ICONERROR);
    app_ui_set_tray_visibility(g_app.show_tray_icon);
}

static LRESULT CALLBACK watch_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (g_app.taskbar_created_message != 0 && message == g_app.taskbar_created_message) {
        app_ui_on_taskbar_created();
        return 0;
    }

    switch (message) {
    case WM_APP_SHOW_SETTINGS:
        app_ui_show_settings();
        return 0;
    case WM_APP_TRAY_CALLBACK:
        app_ui_handle_tray_callback(wparam, lparam);
        return 0;
    case WM_DEVICECHANGE:
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE ||
            wparam == DBT_DEVNODES_CHANGED)
            g_app.needs_apply = 1;
        else if (wparam == DBT_CUSTOMEVENT && lparam != 0) {
            const DEV_BROADCAST_HDR *header = (const DEV_BROADCAST_HDR *)lparam;
            const DEV_BROADCAST_HANDLE *event = (const DEV_BROADCAST_HANDLE *)lparam;
            BTH_HCI_EVENT_INFO info;

            if (header->dbch_devicetype == DBT_DEVTYP_HANDLE &&
                header->dbch_size >= offsetof(DEV_BROADCAST_HANDLE, dbch_data) + sizeof(info) &&
                memcmp(&event->dbch_eventguid, &bluetooth_hci_event_guid, sizeof(GUID)) == 0) {
                memcpy(&info, event->dbch_data, sizeof(info));
                if (info.connected) {
                    g_app.needs_apply = 1;
                    g_app.reconnect_retry = 1;
                    g_app.reconnect_started = GetTickCount();
                }
            }
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)
            g_app.needs_apply = 1;
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_TIMER:
        if (wparam == RETRY_TIMER &&
            (g_app.needs_apply || g_app.reconnect_retry ||
             (DWORD)(GetTickCount() - g_app.last_success) >= REFRESH_INTERVAL_MS))
            try_apply();
        if (g_app.reconnect_retry &&
            (DWORD)(GetTickCount() - g_app.reconnect_started) >= RECONNECT_RETRY_MS)
            g_app.reconnect_retry = 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command)
{
    WNDCLASSW window_definition = {0};
    DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
    app_preferences preferences;
    radio_watch radios[MAX_RADIO_WATCHES] = {{0}};
    size_t radio_count = 0;
    size_t index;
    HANDLE singleton;
    HWND window = NULL;
    HDEVNOTIFY notification = NULL;
    MSG message;
    int exit_code = 0;
    int message_result;
    int hid_initialized = 0;
    int watcher_class_registered = 0;
    int ui_class_registered = 0;
    int timer_started = 0;
    int show_settings_on_start = command_line == NULL || strstr(command_line, "--background") == NULL;

    (void)previous;
    (void)show_command;

    singleton = CreateMutexW(NULL, FALSE, L"Local\\K380FnAutoLock");
    if (singleton == NULL)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        int attempt;
        CloseHandle(singleton);
        window = NULL;
        for (attempt = 0; attempt < 40 && window == NULL; ++attempt) {
            window = FindWindowW(APP_MAIN_WINDOW_CLASS, NULL);
            if (window == NULL)
                Sleep(50);
        }
        if (window != NULL && show_settings_on_start)
            PostMessageW(window, WM_APP_SHOW_SETTINGS, 0, 0);
        return 0;
    }

    g_app.app_instance = instance;
    app_settings_load(&preferences);
    g_app.fn_locked = preferences.saved_fn_locked;
    g_app.show_tray_icon = preferences.saved_show_tray_icon;
    g_app.taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");

    if (hid_init() != 0) {
        exit_code = 1;
        goto cleanup;
    }
    hid_initialized = 1;
    g_app.fn_tray_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                     32, 32, LR_DEFAULTCOLOR | LR_SHARED);

    window_definition.lpfnWndProc = watch_window_proc;
    window_definition.hInstance = instance;
    window_definition.lpszClassName = APP_MAIN_WINDOW_CLASS;
    if (RegisterClassW(&window_definition) == 0) {
        exit_code = 1;
        goto cleanup;
    }
    watcher_class_registered = 1;

    if (!app_ui_register(instance)) {
        exit_code = 1;
        goto cleanup;
    }
    ui_class_registered = 1;

    /* A hidden top-level window receives the registered device notifications. */
    window = CreateWindowExW(0, APP_MAIN_WINDOW_CLASS, L"K380 Fn Auto Lock", WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (window == NULL) {
        exit_code = 1;
        goto cleanup;
    }
    g_app.watcher_window = window;
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = hid_interface_guid;
    notification = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == NULL || SetTimer(window, RETRY_TIMER, RETRY_INTERVAL_MS, NULL) == 0) {
        exit_code = 1;
        goto cleanup;
    }
    timer_started = 1;
    radio_count = watch_bluetooth_radios(window, radios, MAX_RADIO_WATCHES);
    app_ui_set_tray_visibility(g_app.show_tray_icon);

    /* Apply at startup, even if no device-change event is emitted. */
    try_apply();
    if (show_settings_on_start)
        app_ui_show_settings();
    while ((message_result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (message_result < 0)
        exit_code = 1;

cleanup:
    if (timer_started)
        KillTimer(window, RETRY_TIMER);
    for (index = 0; index < radio_count; ++index) {
        UnregisterDeviceNotification(radios[index].notification);
        CloseHandle(radios[index].handle);
    }
    if (notification != NULL)
        UnregisterDeviceNotification(notification);
    if (ui_class_registered)
        app_ui_unregister(instance);
    if (window != NULL)
        DestroyWindow(window);
    if (watcher_class_registered)
        UnregisterClassW(APP_MAIN_WINDOW_CLASS, instance);
    if (hid_initialized)
        hid_exit();
    CloseHandle(singleton);
    return exit_code;
}

#else

int main(void)
{
    k380_apply_result result;

    if (hid_init() != 0) {
        fputs("Cannot initialize HIDAPI\n", stderr);
        return 2;
    }

#ifdef setMediaKeys
    result = k380_apply_key_mode(K380_MODE_MEDIA_KEYS, 0);
#else
    result = k380_apply_key_mode(K380_MODE_FN_LOCKED, 0);
#endif
    if (result == K380_APPLY_NOT_FOUND)
        fputs("K380 was not found\n", stderr);
    else if (result == K380_APPLY_OK)
        puts("K380 key mode updated");

    hid_exit();
    return result;
}

#endif
