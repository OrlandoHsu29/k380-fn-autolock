#include <stdio.h>
#include <hidapi.h>

#ifdef AUTO_WATCH
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>
#include <bluetoothapis.h>
#include <stddef.h>
#include <string.h>
#endif

#define K380_VID 0x046d
#define K380_PID 0xb342
#define K380_USAGE_PAGE 0xff00
#define K380_USAGE 0x0001
#define K380_REPORT_SIZE 7

#ifdef setMediaKeys
static const unsigned char media_keys_report[K380_REPORT_SIZE] = {0x10, 0xff, 0x0b, 0x1e, 0x01, 0x00, 0x00};
#else
static const unsigned char fn_keys_report[K380_REPORT_SIZE] = {0x10, 0xff, 0x0b, 0x1e, 0x00, 0x00, 0x00};
#endif

/* 0: written, 1: no matching interface, 2: open/write failed. */
static int set_key_mode(const unsigned char *report, int quiet)
{
    struct hid_device_info *devices = hid_enumerate(K380_VID, K380_PID);
    struct hid_device_info *device;
    int found = 0;
    int failed = 0;

    for (device = devices; device != NULL; device = device->next) {
        hid_device *handle;
        int written;

        if (device->usage_page != K380_USAGE_PAGE || device->usage != K380_USAGE)
            continue;

        found = 1;
        if (device->path == NULL) {
            failed = 1;
            if (!quiet)
                fputs("K380 HID interface has no path\n", stderr);
            continue;
        }
        handle = hid_open_path(device->path);
        if (handle == NULL) {
            failed = 1;
            if (!quiet)
                fputs("Cannot open K380 HID interface\n", stderr);
            continue;
        }

        written = hid_write(handle, report, K380_REPORT_SIZE);
        if (written != K380_REPORT_SIZE) {
            failed = 1;
            if (!quiet)
                fprintf(stderr, "Cannot set K380 mode (%d bytes written)\n", written);
        }
        hid_close(handle);
    }

    hid_free_enumeration(devices);
    return !found ? 1 : (failed ? 2 : 0);
}

#ifdef AUTO_WATCH

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

static int needs_apply = 1;
static DWORD last_success = 0;
static int reconnect_retry = 0;
static DWORD reconnect_started = 0;

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
    if (set_key_mode(fn_keys_report, 1) == 0) {
        needs_apply = 0;
        last_success = GetTickCount();
        OutputDebugStringA("K380 Fn mode applied\n");
    }
}

static LRESULT CALLBACK watch_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_DEVICECHANGE:
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE ||
            wparam == DBT_DEVNODES_CHANGED)
            needs_apply = 1;
        else if (wparam == DBT_CUSTOMEVENT && lparam != 0) {
            const DEV_BROADCAST_HDR *header = (const DEV_BROADCAST_HDR *)lparam;
            const DEV_BROADCAST_HANDLE *event = (const DEV_BROADCAST_HANDLE *)lparam;
            BTH_HCI_EVENT_INFO info;

            if (header->dbch_devicetype == DBT_DEVTYP_HANDLE &&
                header->dbch_size >= offsetof(DEV_BROADCAST_HANDLE, dbch_data) + sizeof(info) &&
                memcmp(&event->dbch_eventguid, &bluetooth_hci_event_guid, sizeof(GUID)) == 0) {
                memcpy(&info, event->dbch_data, sizeof(info));
                if (info.connected) {
                    needs_apply = 1;
                    reconnect_retry = 1;
                    reconnect_started = GetTickCount();
                }
            }
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)
            needs_apply = 1;
        return TRUE;
    case WM_TIMER:
        if (wparam == RETRY_TIMER &&
            (needs_apply || reconnect_retry ||
             (DWORD)(GetTickCount() - last_success) >= REFRESH_INTERVAL_MS))
            try_apply();
        if (reconnect_retry &&
            (DWORD)(GetTickCount() - reconnect_started) >= RECONNECT_RETRY_MS)
            reconnect_retry = 0;
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
    static const wchar_t window_class[] = L"K380FnAutoLockWindow";
    WNDCLASSW window_definition = {0};
    DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
    HANDLE singleton;
    HWND window;
    HDEVNOTIFY notification = NULL;
    radio_watch radios[MAX_RADIO_WATCHES] = {{0}};
    size_t radio_count = 0;
    size_t index;
    MSG message;
    int exit_code = 0;
    int message_result;

    (void)previous;
    (void)command_line;
    (void)show_command;

    singleton = CreateMutexW(NULL, FALSE, L"Local\\K380FnAutoLock");
    if (singleton == NULL)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleton);
        return 0;
    }

    if (hid_init() != 0) {
        CloseHandle(singleton);
        return 1;
    }

    window_definition.lpfnWndProc = watch_window_proc;
    window_definition.hInstance = instance;
    window_definition.lpszClassName = window_class;
    if (RegisterClassW(&window_definition) == 0) {
        exit_code = 1;
        goto done;
    }

    /* A hidden top-level window receives the registered device notifications. */
    window = CreateWindowExW(0, window_class, L"K380 Fn Auto Lock", WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (window == NULL) {
        exit_code = 1;
        goto unregister_class;
    }

    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = hid_interface_guid;
    notification = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == NULL || SetTimer(window, RETRY_TIMER, RETRY_INTERVAL_MS, NULL) == 0) {
        exit_code = 1;
        goto destroy_window;
    }
    radio_count = watch_bluetooth_radios(window, radios, MAX_RADIO_WATCHES);

    /* Apply at login, even if no device-change event is emitted. */
    try_apply();
    while ((message_result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (message_result < 0)
        exit_code = 1;

destroy_window:
    KillTimer(window, RETRY_TIMER);
    for (index = 0; index < radio_count; ++index) {
        UnregisterDeviceNotification(radios[index].notification);
        CloseHandle(radios[index].handle);
    }
    if (notification != NULL)
        UnregisterDeviceNotification(notification);
    DestroyWindow(window);
unregister_class:
    UnregisterClassW(window_class, instance);
done:
    hid_exit();
    CloseHandle(singleton);
    return exit_code;
}

#else

int main(void)
{
    int result;
    if (hid_init() != 0) {
        fputs("Cannot initialize HIDAPI\n", stderr);
        return 2;
    }

#ifdef setMediaKeys
    result = set_key_mode(media_keys_report, 0);
#else
    result = set_key_mode(fn_keys_report, 0);
#endif

    if (result == 1)
        fputs("K380 was not found\n", stderr);
    else if (result == 0)
        puts("K380 key mode updated");

    hid_exit();
    return result;
}

#endif
