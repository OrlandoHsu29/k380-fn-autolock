#include <stdio.h>
#include <hidapi.h>

#ifdef AUTO_WATCH
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>
#include <bluetoothapis.h>
#include <shellapi.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#endif

#define K380_VID 0x046d
#define K380_PID 0xb342
#define K380_USAGE_PAGE 0xff00
#define K380_USAGE 0x0001
#define K380_REPORT_SIZE 7

#ifdef AUTO_WATCH
#define IDI_APP_ICON 101
static const unsigned char fn_keys_report[K380_REPORT_SIZE] = {0x10, 0xff, 0x0b, 0x1e, 0x00, 0x00, 0x00};
static const unsigned char media_keys_report[K380_REPORT_SIZE] = {0x10, 0xff, 0x0b, 0x1e, 0x01, 0x00, 0x00};
#elif defined(setMediaKeys)
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
#define TRAY_ICON_ID 1
#define WM_APP_SHOW_SETTINGS (WM_APP + 1)
#define WM_APP_TRAY_CALLBACK (WM_APP + 2)
#define REGISTRY_RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REGISTRY_PREFS_KEY L"Software\\K380FnAutoLock"
#define REGISTRY_RUN_VALUE L"K380FnAutoLock"
#define REGISTRY_FN_LOCKED L"FnLocked"
#define REGISTRY_SHOW_TRAY L"ShowTrayIcon"
#define IDC_STARTUP_CHECKBOX 101
#define IDC_FN_LOCK_RADIO 102
#define IDC_FN_UNLOCK_RADIO 103
#define IDC_TRAY_CHECKBOX 104
#define IDC_CLOSE_BUTTON 105
#define IDM_OPEN_SETTINGS 201
#define IDM_TOGGLE_STARTUP 202
#define IDM_FN_LOCK 203
#define IDM_FN_UNLOCK 204
#define IDM_HIDE_TRAY 205
#define IDM_EXIT_PROGRAM 206

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
static int fn_locked = 1;
static int show_tray_icon = 1;
static int tray_icon_added = 0;
static int last_apply_result = 1;
static int autostart_enabled = 0;
static HINSTANCE app_instance;
static HWND settings_window;
static HWND startup_checkbox;
static HWND fn_lock_radio;
static HWND fn_unlock_radio;
static HWND tray_checkbox;
static HWND status_label;
static UINT taskbar_created_message;
static HFONT settings_font;
static int owns_settings_font;
static HWND watcher_window;
static HICON fn_tray_icon;

static const wchar_t main_window_class[] = L"K380FnAutoLockWindow";
static const wchar_t settings_window_class[] = L"K380FnAutoLockSettingsWindow";

static void apply_settings_font(HWND control)
{
    if (control != NULL && settings_font != NULL)
        SendMessageW(control, WM_SETFONT, (WPARAM)settings_font, TRUE);
}

static void draw_settings_button(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem;
    wchar_t label[96] = L"";
    int control_id = (int)item->CtlID;
    int checked = 0;
    int disabled = (item->itemState & ODS_DISABLED) != 0;
    int is_radio = control_id == IDC_FN_LOCK_RADIO || control_id == IDC_FN_UNLOCK_RADIO;
    int is_close = control_id == IDC_CLOSE_BUTTON;
    HDC dc = item->hDC;
    HPEN pen;
    HGDIOBJ old_pen;
    HGDIOBJ old_font = NULL;
    HBRUSH old_brush;
    RECT mark;

    FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
    if (control_id == IDC_STARTUP_CHECKBOX)
        checked = autostart_enabled;
    else if (control_id == IDC_FN_LOCK_RADIO)
        checked = fn_locked;
    else if (control_id == IDC_FN_UNLOCK_RADIO)
        checked = !fn_locked;
    else if (control_id == IDC_TRAY_CHECKBOX)
        checked = show_tray_icon;

    GetWindowTextW(item->hwndItem, label, sizeof(label) / sizeof(label[0]));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(145, 145, 145) : RGB(38, 42, 48));
    if (settings_font != NULL)
        old_font = SelectObject(dc, settings_font);

    if (is_close) {
        HBRUSH button_brush = CreateSolidBrush((item->itemState & ODS_SELECTED)
                                                   ? RGB(166, 37, 42)
                                                   : RGB(198, 48, 54));
        HBRUSH border_brush = CreateSolidBrush(RGB(155, 32, 38));
        FillRect(dc, &rect, button_brush);
        FrameRect(dc, &rect, border_brush);
        DeleteObject(button_brush);
        DeleteObject(border_brush);
        SetTextColor(dc, RGB(255, 255, 255));
        rect.top += (item->itemState & ODS_SELECTED) ? 1 : 0;
        DrawTextW(dc, label, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if ((item->itemState & ODS_FOCUS) != 0)
            DrawFocusRect(dc, &rect);
        if (old_font != NULL)
            SelectObject(dc, old_font);
        return;
    }

    mark.left = rect.left + 2;
    mark.top = rect.top + ((rect.bottom - rect.top) - 14) / 2;
    mark.right = mark.left + 14;
    mark.bottom = mark.top + 14;
    pen = CreatePen(PS_SOLID, 1, disabled ? RGB(175, 175, 175) : RGB(112, 120, 130));
    old_pen = SelectObject(dc, pen);
    old_brush = SelectObject(dc, GetSysColorBrush(COLOR_WINDOW));
    if (is_radio)
        Ellipse(dc, mark.left, mark.top, mark.right, mark.bottom);
    else
        Rectangle(dc, mark.left, mark.top, mark.right, mark.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    if (checked) {
        HPEN check_pen = CreatePen(PS_SOLID, 2, disabled ? RGB(125, 150, 180) : RGB(42, 112, 204));
        old_pen = SelectObject(dc, check_pen);
        if (is_radio) {
            HBRUSH dot = CreateSolidBrush(disabled ? RGB(125, 150, 180) : RGB(42, 112, 204));
            old_brush = SelectObject(dc, dot);
            Ellipse(dc, mark.left + 4, mark.top + 4, mark.right - 4, mark.bottom - 4);
            SelectObject(dc, old_brush);
            DeleteObject(dot);
        } else {
            MoveToEx(dc, mark.left + 3, mark.top + 7, NULL);
            LineTo(dc, mark.left + 6, mark.top + 10);
            LineTo(dc, mark.left + 12, mark.top + 3);
        }
        SelectObject(dc, old_pen);
        DeleteObject(check_pen);
    }

    rect.left = mark.right + 9;
    DrawTextW(dc, label, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    if ((item->itemState & ODS_FOCUS) != 0)
        DrawFocusRect(dc, &item->rcItem);
    if (old_font != NULL)
        SelectObject(dc, old_font);
}

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

static int is_autostart_enabled(void)
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

static int set_autostart_enabled(int enabled)
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
        if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) {
            autostart_enabled = 0;
            return 1;
        }
        return 0;
    }

    {
        wchar_t executable[MAX_PATH];
        wchar_t command[MAX_PATH + 4];
        DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
        DWORD bytes;

        if (length == 0 || length >= MAX_PATH)
            return 0;
        command[0] = L'"';
        memcpy(command + 1, executable, length * sizeof(wchar_t));
        command[length + 1] = L'"';
        command[length + 2] = L'\0';
        bytes = (length + 3) * sizeof(wchar_t);

        result = RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &key, NULL);
        if (result != ERROR_SUCCESS)
            return 0;
        result = RegSetValueExW(key, REGISTRY_RUN_VALUE, 0, REG_SZ, (const BYTE *)command, bytes);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS)
            autostart_enabled = 1;
        return result == ERROR_SUCCESS;
    }
}

static void update_settings_window(void)
{
    wchar_t status[128];

    if (settings_window == NULL)
        return;
    autostart_enabled = is_autostart_enabled();
    if (fn_lock_radio != NULL) {
        EnableWindow(fn_lock_radio, !fn_locked);
        InvalidateRect(fn_lock_radio, NULL, TRUE);
    }
    if (fn_unlock_radio != NULL) {
        EnableWindow(fn_unlock_radio, fn_locked);
        InvalidateRect(fn_unlock_radio, NULL, TRUE);
    }
    if (startup_checkbox != NULL)
        InvalidateRect(startup_checkbox, NULL, TRUE);
    if (tray_checkbox != NULL)
        InvalidateRect(tray_checkbox, NULL, TRUE);
    if (status_label != NULL) {
        if (last_apply_result == 0)
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：已应用%ls",
                     fn_locked ? L" Fn 锁定" : L"媒体键优先");
        else if (last_apply_result == 1)
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：等待 K380 连接，将自动应用%ls",
                     fn_locked ? L" Fn 锁定" : L"媒体键优先");
        else
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：HID 接口暂不可写，将继续重试");
        SetWindowTextW(status_label, status);
    }
}

static void add_tray_icon(HWND window)
{
    NOTIFYICONDATAW icon = {0};

    if (!show_tray_icon || tray_icon_added)
        return;
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = TRAY_ICON_ID;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_APP_TRAY_CALLBACK;
    icon.hIcon = fn_tray_icon != NULL ? fn_tray_icon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wcscpy(icon.szTip, L"K380 Fn Auto Lock");
    tray_icon_added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

static void set_tray_visibility(int visible)
{
    HWND window = FindWindowW(main_window_class, NULL);
    NOTIFYICONDATAW icon = {0};

    show_tray_icon = visible != 0;
    if (!write_preference(REGISTRY_SHOW_TRAY, show_tray_icon ? 1 : 0))
        MessageBoxW(settings_window, L"图标显示设置无法保存；本次运行仍会按当前选择处理。",
                    L"K380 Fn 设置", MB_OK | MB_ICONERROR);
    if (window == NULL)
        return;
    if (show_tray_icon) {
        add_tray_icon(window);
    } else if (tray_icon_added) {
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = TRAY_ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        tray_icon_added = 0;
    }
    update_settings_window();
}

static void show_settings(void)
{
    RECT rect = {0, 0, 360, 220};
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    DWORD ex_style = WS_EX_TOOLWINDOW;
    int width;
    int height;

    if (settings_window == NULL) {
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        settings_window = CreateWindowExW(ex_style, settings_window_class,
                                          L"K380 Fn 设置", style,
                                          (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                                          (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                          width, height, NULL, NULL, app_instance, NULL);
        if (settings_window == NULL)
            return;
        if (fn_tray_icon != NULL) {
            SendMessageW(settings_window, WM_SETICON, ICON_SMALL, (LPARAM)fn_tray_icon);
            SendMessageW(settings_window, WM_SETICON, ICON_BIG, (LPARAM)fn_tray_icon);
        }
    }
    update_settings_window();
    ShowWindow(settings_window, SW_SHOWNORMAL);
    SetForegroundWindow(settings_window);
}

static void set_desired_mode(int lock_fn)
{
    fn_locked = lock_fn != 0;
    if (!write_preference(REGISTRY_FN_LOCKED, fn_locked ? 1 : 0))
        MessageBoxW(settings_window, L"Fn 模式无法保存；本次运行仍会按当前选择处理。",
                    L"K380 Fn 设置", MB_OK | MB_ICONERROR);
    needs_apply = 1;
    last_apply_result = set_key_mode(fn_locked ? fn_keys_report : media_keys_report, 1);
    if (last_apply_result == 0) {
        needs_apply = 0;
        last_success = GetTickCount();
    }
    update_settings_window();
}

static void handle_menu_command(UINT command)
{
    switch (command) {
    case IDM_OPEN_SETTINGS:
        show_settings();
        break;
    case IDM_TOGGLE_STARTUP:
        if (!set_autostart_enabled(!is_autostart_enabled()))
            MessageBoxW(settings_window, L"无法更新当前用户的开机启动设置。", L"K380 Fn 设置", MB_OK | MB_ICONERROR);
        update_settings_window();
        break;
    case IDM_FN_LOCK:
        set_desired_mode(1);
        break;
    case IDM_FN_UNLOCK:
        set_desired_mode(0);
        break;
    case IDM_HIDE_TRAY:
        set_tray_visibility(0);
        break;
    case IDM_EXIT_PROGRAM:
        if (watcher_window != NULL)
            PostMessageW(watcher_window, WM_CLOSE, 0, 0);
        break;
    }
}

static void show_tray_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    POINT cursor;
    UINT startup_flags = MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0);
    UINT lock_flags = MF_STRING | (fn_locked ? (MF_CHECKED | MF_GRAYED) : 0);
    UINT unlock_flags = MF_STRING | (!fn_locked ? (MF_CHECKED | MF_GRAYED) : 0);
    UINT command;

    if (menu == NULL)
        return;
    AppendMenuW(menu, MF_STRING, IDM_OPEN_SETTINGS, L"打开设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, startup_flags, IDM_TOGGLE_STARTUP, L"登录时自动启动");
    AppendMenuW(menu, lock_flags, IDM_FN_LOCK, L"Fn 键锁定（F1–F12 优先）");
    AppendMenuW(menu, unlock_flags, IDM_FN_UNLOCK, L"解除 Fn 锁定（媒体键优先）");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_HIDE_TRAY, L"隐藏任务栏图标");
    AppendMenuW(menu, MF_STRING, IDM_EXIT_PROGRAM, L"退出程序");

    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             cursor.x, cursor.y, 0, window, NULL);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command != 0)
        handle_menu_command(command);
}

static LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        {
            HDC dc = GetDC(window);
            int font_height = dc != NULL ? -MulDiv(9, GetDeviceCaps(dc, LOGPIXELSY), 72) : -12;
            HWND label;
            HWND close_button;

            if (dc != NULL)
                ReleaseDC(window, dc);
            settings_font = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            owns_settings_font = settings_font != NULL;
            if (settings_font == NULL)
                settings_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        startup_checkbox = CreateWindowExW(0, L"BUTTON", L"登录时自动启动",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            16, 14, 300, 24, window, (HMENU)(INT_PTR)IDC_STARTUP_CHECKBOX, app_instance, NULL);
        label = CreateWindowExW(0, L"STATIC", L"Fn 键模式：", WS_CHILD | WS_VISIBLE,
            16, 48, 100, 20, window, NULL, app_instance, NULL);
        fn_lock_radio = CreateWindowExW(0, L"BUTTON", L"锁定 Fn（F1–F12 优先）",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_OWNERDRAW,
            26, 68, 280, 22, window, (HMENU)(INT_PTR)IDC_FN_LOCK_RADIO, app_instance, NULL);
        fn_unlock_radio = CreateWindowExW(0, L"BUTTON", L"解除锁定（媒体键优先）",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            26, 92, 280, 22, window, (HMENU)(INT_PTR)IDC_FN_UNLOCK_RADIO, app_instance, NULL);
        tray_checkbox = CreateWindowExW(0, L"BUTTON", L"显示任务栏图标",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            16, 124, 300, 24, window, (HMENU)(INT_PTR)IDC_TRAY_CHECKBOX, app_instance, NULL);
        status_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            16, 154, 325, 20, window, NULL, app_instance, NULL);
        close_button = CreateWindowExW(0, L"BUTTON", L"退出程序", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            128, 181, 104, 28, window, (HMENU)(INT_PTR)IDC_CLOSE_BUTTON, app_instance, NULL);
        apply_settings_font(startup_checkbox);
        apply_settings_font(label);
        apply_settings_font(fn_lock_radio);
        apply_settings_font(fn_unlock_radio);
        apply_settings_font(tray_checkbox);
        apply_settings_font(status_label);
        apply_settings_font(close_button);
        update_settings_window();
        return 0;
        }
    case WM_ERASEBKGND:
        {
            RECT client;
            GetClientRect(window, &client);
            FillRect((HDC)wparam, &client, GetSysColorBrush(COLOR_WINDOW));
        }
        return 1;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wparam, TRANSPARENT);
        SetTextColor((HDC)wparam, RGB(60, 64, 72));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DRAWITEM:
        if (lparam != 0 && ((DRAWITEMSTRUCT *)lparam)->CtlType == ODT_BUTTON) {
            draw_settings_button((const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_STARTUP_CHECKBOX:
            if (!set_autostart_enabled(!is_autostart_enabled()))
                MessageBoxW(window, L"无法更新当前用户的开机启动设置。", L"K380 Fn 设置", MB_OK | MB_ICONERROR);
            update_settings_window();
            return 0;
        case IDC_FN_LOCK_RADIO:
            set_desired_mode(1);
            return 0;
        case IDC_FN_UNLOCK_RADIO:
            set_desired_mode(0);
            return 0;
        case IDC_TRAY_CHECKBOX:
            set_tray_visibility(!show_tray_icon);
            return 0;
        case IDC_CLOSE_BUTTON:
            if (watcher_window != NULL)
                PostMessageW(watcher_window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (owns_settings_font && settings_font != NULL)
            DeleteObject(settings_font);
        settings_font = NULL;
        owns_settings_font = 0;
        startup_checkbox = NULL;
        fn_lock_radio = NULL;
        fn_unlock_radio = NULL;
        tray_checkbox = NULL;
        status_label = NULL;
        settings_window = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

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
    last_apply_result = set_key_mode(fn_locked ? fn_keys_report : media_keys_report, 1);
    if (last_apply_result == 0) {
        needs_apply = 0;
        last_success = GetTickCount();
        OutputDebugStringA(fn_locked ? "K380 Fn mode applied\n" : "K380 media mode applied\n");
    }
    update_settings_window();
}

static LRESULT CALLBACK watch_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (taskbar_created_message != 0 && message == taskbar_created_message) {
        tray_icon_added = 0;
        add_tray_icon(window);
        return 0;
    }

    switch (message) {
    case WM_APP_SHOW_SETTINGS:
        show_settings();
        return 0;
    case WM_APP_TRAY_CALLBACK:
        if ((UINT)wparam != TRAY_ICON_ID)
            return 0;
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)
            show_tray_menu(window);
        else if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK)
            show_settings();
        return 0;
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
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
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
    WNDCLASSW window_definition = {0};
    WNDCLASSW settings_definition = {0};
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
        int attempt;
        CloseHandle(singleton);
        window = NULL;
        for (attempt = 0; attempt < 40 && window == NULL; ++attempt) {
            window = FindWindowW(main_window_class, NULL);
            if (window == NULL)
                Sleep(50);
        }
        if (window != NULL)
            PostMessageW(window, WM_APP_SHOW_SETTINGS, 0, 0);
        return 0;
    }

    app_instance = instance;
    fn_locked = read_preference(REGISTRY_FN_LOCKED, 1) != 0;
    show_tray_icon = read_preference(REGISTRY_SHOW_TRAY, 1) != 0;
    taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");

    if (hid_init() != 0) {
        CloseHandle(singleton);
        return 1;
    }
    fn_tray_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                     32, 32, LR_DEFAULTCOLOR | LR_SHARED);

    window_definition.lpfnWndProc = watch_window_proc;
    window_definition.hInstance = instance;
    window_definition.lpszClassName = main_window_class;
    if (RegisterClassW(&window_definition) == 0) {
        exit_code = 1;
        goto done;
    }

    settings_definition.lpfnWndProc = settings_window_proc;
    settings_definition.hInstance = instance;
    settings_definition.lpszClassName = settings_window_class;
    if (RegisterClassW(&settings_definition) == 0) {
        exit_code = 1;
        goto unregister_class;
    }

    /* A hidden top-level window receives the registered device notifications. */
    window = CreateWindowExW(0, main_window_class, L"K380 Fn Auto Lock", WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (window == NULL) {
        exit_code = 1;
        goto unregister_class;
    }
    watcher_window = window;
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = hid_interface_guid;
    notification = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == NULL || SetTimer(window, RETRY_TIMER, RETRY_INTERVAL_MS, NULL) == 0) {
        exit_code = 1;
        goto destroy_window;
    }
    radio_count = watch_bluetooth_radios(window, radios, MAX_RADIO_WATCHES);
    add_tray_icon(window);

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
    if (tray_icon_added) {
        NOTIFYICONDATAW icon = {0};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = TRAY_ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        tray_icon_added = 0;
    }
    if (settings_window != NULL)
        DestroyWindow(settings_window);
    DestroyWindow(window);
unregister_class:
    UnregisterClassW(main_window_class, instance);
    UnregisterClassW(settings_window_class, instance);
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
