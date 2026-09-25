#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#include "app_state.h"
#include "app_settings.h"
#include "app_runtime.h"
#include "app_ui.h"

#define IDC_STARTUP_CHECKBOX 101
#define IDC_FN_LOCK_RADIO 102
#define IDC_FN_UNLOCK_RADIO 103
#define IDC_TRAY_CHECKBOX 104
#define IDC_CLOSE_BUTTON 105
#define IDC_OK_BUTTON 106
#define IDM_OPEN_SETTINGS 201
#define IDM_TOGGLE_STARTUP 202
#define IDM_FN_LOCK 203
#define IDM_FN_UNLOCK 204
#define IDM_HIDE_TRAY 205
#define IDM_EXIT_PROGRAM 206
#define APP_VERSION L"v1.0.3"

static const wchar_t settings_window_class[] = L"K380FnAutoLockSettingsWindow";
static HFONT settings_title_font;
static HWND settings_title_label;
static int settings_draft_valid;
static int settings_draft_fn_locked;
static int settings_draft_tray_visible;
static int settings_draft_autostart_enabled;

static void apply_settings_font(HWND control)
{
    if (control != NULL && g_app.settings_font != NULL)
        SendMessageW(control, WM_SETFONT, (WPARAM)g_app.settings_font, TRUE);
}

static int commit_settings(HWND window)
{
    if (!app_settings_set_autostart_enabled(settings_draft_autostart_enabled)) {
        MessageBoxW(window, L"无法更新当前用户的开机启动设置。", L"K380 Fn 设置", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (settings_draft_fn_locked != g_app.fn_locked)
        app_runtime_set_mode(settings_draft_fn_locked);
    if (settings_draft_tray_visible != g_app.show_tray_icon)
        app_runtime_set_tray_visibility(settings_draft_tray_visible);
    app_ui_update();
    return 1;
}

static int settings_have_unsaved_changes(void)
{
    return settings_draft_valid &&
           (settings_draft_fn_locked != g_app.fn_locked ||
            settings_draft_tray_visible != g_app.show_tray_icon ||
            settings_draft_autostart_enabled != app_settings_autostart_enabled());
}

static void close_settings_and_restart(HWND window)
{
    DestroyWindow(window);
    app_runtime_restart_in_background();
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
    int is_ok = control_id == IDC_OK_BUTTON;
    HDC dc = item->hDC;
    HPEN pen;
    HGDIOBJ old_pen;
    HGDIOBJ old_font = NULL;
    HBRUSH old_brush;
    RECT mark;

    FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
    if (control_id == IDC_STARTUP_CHECKBOX)
        checked = settings_draft_valid ? settings_draft_autostart_enabled : g_app.autostart_enabled;
    else if (control_id == IDC_FN_LOCK_RADIO)
        checked = settings_draft_valid ? settings_draft_fn_locked : g_app.fn_locked;
    else if (control_id == IDC_FN_UNLOCK_RADIO)
        checked = !(settings_draft_valid ? settings_draft_fn_locked : g_app.fn_locked);
    else if (control_id == IDC_TRAY_CHECKBOX)
        checked = !(settings_draft_valid ? settings_draft_tray_visible : g_app.show_tray_icon);

    GetWindowTextW(item->hwndItem, label, sizeof(label) / sizeof(label[0]));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(145, 145, 145) : RGB(38, 42, 48));
    if (g_app.settings_font != NULL)
        old_font = SelectObject(dc, g_app.settings_font);

    if (is_close || is_ok) {
        COLORREF button_color = is_close
            ? ((item->itemState & ODS_SELECTED) ? RGB(166, 37, 42) : RGB(198, 48, 54))
            : ((item->itemState & ODS_SELECTED) ? RGB(240, 243, 247) : RGB(255, 255, 255));
        COLORREF border_color = is_close ? RGB(155, 32, 38) : RGB(190, 196, 204);
        HBRUSH button_brush = CreateSolidBrush(button_color);
        HBRUSH border_brush = CreateSolidBrush(border_color);
        FillRect(dc, &rect, button_brush);
        FrameRect(dc, &rect, border_brush);
        DeleteObject(button_brush);
        DeleteObject(border_brush);
        SetTextColor(dc, is_close ? RGB(255, 255, 255) : RGB(38, 42, 48));
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

void app_ui_update(void)
{
    wchar_t status[128];
    int fn_locked;
    int autostart_enabled;
    int tray_hidden;
    int fn_mode_changed;
    const wchar_t *mode_status;

    if (g_app.settings_window == NULL)
        return;
    g_app.autostart_enabled = app_settings_autostart_enabled();
    fn_mode_changed = settings_draft_valid && settings_draft_fn_locked != g_app.fn_locked;
    if (g_app.startup_checkbox != NULL)
        SetWindowTextW(g_app.startup_checkbox,
                       settings_draft_valid && settings_draft_autostart_enabled != g_app.autostart_enabled
                           ? L"开机时自动启动*"
                           : L"开机时自动启动");
    if (g_app.fn_lock_radio != NULL) {
        SetWindowTextW(g_app.fn_lock_radio,
                       fn_mode_changed && settings_draft_fn_locked
                           ? L"锁定 Fn（F1–F12 优先）*"
                           : L"锁定 Fn（F1–F12 优先）");
        EnableWindow(g_app.fn_lock_radio, !(settings_draft_valid ? settings_draft_fn_locked : g_app.fn_locked));
        InvalidateRect(g_app.fn_lock_radio, NULL, TRUE);
    }
    if (g_app.fn_unlock_radio != NULL) {
        SetWindowTextW(g_app.fn_unlock_radio,
                       fn_mode_changed && !settings_draft_fn_locked
                           ? L"解除锁定（媒体键优先）*"
                           : L"解除锁定（媒体键优先）");
        EnableWindow(g_app.fn_unlock_radio, settings_draft_valid ? settings_draft_fn_locked : g_app.fn_locked);
        InvalidateRect(g_app.fn_unlock_radio, NULL, TRUE);
    }
    if (g_app.startup_checkbox != NULL)
        InvalidateRect(g_app.startup_checkbox, NULL, TRUE);
    if (g_app.tray_checkbox != NULL) {
        SetWindowTextW(g_app.tray_checkbox,
                       settings_draft_valid && settings_draft_tray_visible != g_app.show_tray_icon
                           ? L"隐藏任务栏图标*"
                           : L"隐藏任务栏图标");
        InvalidateRect(g_app.tray_checkbox, NULL, TRUE);
    }
    SetWindowTextW(g_app.settings_window,
                   settings_have_unsaved_changes()
                       ? L"K380 Fn Auto Lock " APP_VERSION L"*"
                       : L"K380 Fn Auto Lock " APP_VERSION);
    if (g_app.status_label != NULL) {
        fn_locked = g_app.fn_locked;
        autostart_enabled = g_app.autostart_enabled;
        tray_hidden = !g_app.show_tray_icon;
        mode_status = fn_locked ? L"F 键优先" : L"媒体键优先";
        if (autostart_enabled && tray_hidden)
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：开机自启、%ls、隐藏图标", mode_status);
        else if (autostart_enabled)
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：开机自启、%ls", mode_status);
        else if (tray_hidden)
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：%ls、隐藏图标", mode_status);
        else
            swprintf(status, sizeof(status) / sizeof(status[0]), L"状态：%ls", mode_status);
        SetWindowTextW(g_app.status_label, status);
    }
}

static void app_ui_add_tray_icon(HWND window)
{
    NOTIFYICONDATAW icon = {0};

    if (!g_app.show_tray_icon || g_app.tray_icon_added)
        return;
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = TRAY_ICON_ID;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_APP_TRAY_CALLBACK;
    icon.hIcon = g_app.fn_tray_icon != NULL ? g_app.fn_tray_icon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wcscpy(icon.szTip, L"K380 Fn Auto Lock " APP_VERSION);
    g_app.tray_icon_added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void app_ui_show_settings(void)
{
    RECT rect = {0, 0, 360, 248};
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    DWORD ex_style = WS_EX_APPWINDOW;
    int width;
    int height;

    if (g_app.settings_window == NULL) {
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        g_app.settings_window = CreateWindowExW(ex_style, settings_window_class,
                                          L"K380 Fn Auto Lock " APP_VERSION, style,
                                          (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                                          (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                          width, height, NULL, NULL, g_app.app_instance, NULL);
        if (g_app.settings_window == NULL)
            return;
        if (g_app.fn_tray_icon != NULL) {
            SendMessageW(g_app.settings_window, WM_SETICON, ICON_SMALL, (LPARAM)g_app.fn_tray_icon);
            SendMessageW(g_app.settings_window, WM_SETICON, ICON_BIG, (LPARAM)g_app.fn_tray_icon);
        }
    }
    app_ui_update();
    ShowWindow(g_app.settings_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_app.settings_window);
}

static void handle_menu_command(UINT command)
{
    switch (command) {
    case IDM_OPEN_SETTINGS:
        app_ui_show_settings();
        break;
    case IDM_TOGGLE_STARTUP:
        if (!app_settings_set_autostart_enabled(!app_settings_autostart_enabled()))
            MessageBoxW(g_app.settings_window, L"无法更新当前用户的开机启动设置。", L"K380 Fn 设置", MB_OK | MB_ICONERROR);
        app_ui_update();
        break;
    case IDM_FN_LOCK:
        app_runtime_set_mode(1);
        break;
    case IDM_FN_UNLOCK:
        app_runtime_set_mode(0);
        break;
    case IDM_HIDE_TRAY:
        app_runtime_set_tray_visibility(0);
        break;
    case IDM_EXIT_PROGRAM:
        if (g_app.watcher_window != NULL)
            PostMessageW(g_app.watcher_window, WM_CLOSE, 0, 0);
        break;
    }
}

static void show_tray_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    POINT cursor;
    UINT startup_flags = MF_STRING | (app_settings_autostart_enabled() ? MF_CHECKED : 0);
    UINT lock_flags = MF_STRING | (g_app.fn_locked ? (MF_CHECKED | MF_GRAYED) : 0);
    UINT unlock_flags = MF_STRING | (!g_app.fn_locked ? (MF_CHECKED | MF_GRAYED) : 0);
    UINT command;

    if (menu == NULL)
        return;
    AppendMenuW(menu, MF_STRING, IDM_OPEN_SETTINGS, L"打开设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, startup_flags, IDM_TOGGLE_STARTUP, L"开机时自动启动");
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
            int dpi_y = dc != NULL ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
            int font_height = -MulDiv(9, dpi_y, 72);
            int title_font_height = -MulDiv(11, dpi_y, 72);
            HWND label;
            HWND ok_button;
            HWND close_button;

            settings_draft_fn_locked = g_app.fn_locked;
            settings_draft_tray_visible = g_app.show_tray_icon;
            settings_draft_autostart_enabled = app_settings_autostart_enabled();
            settings_draft_valid = 1;

            if (dc != NULL)
                ReleaseDC(window, dc);
            g_app.settings_font = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.owns_settings_font = g_app.settings_font != NULL;
            if (g_app.settings_font == NULL)
                g_app.settings_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            settings_title_font = CreateFontW(title_font_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        settings_title_label = CreateWindowExW(0, L"STATIC", L"K380 Fn 设置",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            16, 10, 320, 24, window, NULL, g_app.app_instance, NULL);
        g_app.status_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
            16, 36, 328, 20, window, NULL, g_app.app_instance, NULL);
        g_app.startup_checkbox = CreateWindowExW(0, L"BUTTON", L"开机时自动启动",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            16, 64, 300, 24, window, (HMENU)(INT_PTR)IDC_STARTUP_CHECKBOX, g_app.app_instance, NULL);
        label = CreateWindowExW(0, L"STATIC", L"Fn 键模式：", WS_CHILD | WS_VISIBLE,
            16, 98, 100, 20, window, NULL, g_app.app_instance, NULL);
        g_app.fn_lock_radio = CreateWindowExW(0, L"BUTTON", L"锁定 Fn（F1–F12 优先）",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_OWNERDRAW,
            26, 118, 280, 22, window, (HMENU)(INT_PTR)IDC_FN_LOCK_RADIO, g_app.app_instance, NULL);
        g_app.fn_unlock_radio = CreateWindowExW(0, L"BUTTON", L"解除锁定（媒体键优先）",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            26, 142, 280, 22, window, (HMENU)(INT_PTR)IDC_FN_UNLOCK_RADIO, g_app.app_instance, NULL);
        g_app.tray_checkbox = CreateWindowExW(0, L"BUTTON", L"隐藏任务栏图标",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            16, 174, 300, 24, window, (HMENU)(INT_PTR)IDC_TRAY_CHECKBOX, g_app.app_instance, NULL);
        ok_button = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            68, 209, 104, 28, window, (HMENU)(INT_PTR)IDC_OK_BUTTON, g_app.app_instance, NULL);
        close_button = CreateWindowExW(0, L"BUTTON", L"退出程序", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            188, 209, 104, 28, window, (HMENU)(INT_PTR)IDC_CLOSE_BUTTON, g_app.app_instance, NULL);
        if (settings_title_font != NULL)
            SendMessageW(settings_title_label, WM_SETFONT, (WPARAM)settings_title_font, TRUE);
        else
            apply_settings_font(settings_title_label);
        apply_settings_font(g_app.startup_checkbox);
        apply_settings_font(label);
        apply_settings_font(g_app.fn_lock_radio);
        apply_settings_font(g_app.fn_unlock_radio);
        apply_settings_font(g_app.tray_checkbox);
        apply_settings_font(g_app.status_label);
        apply_settings_font(ok_button);
        apply_settings_font(close_button);
        app_ui_update();
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
            settings_draft_autostart_enabled = !settings_draft_autostart_enabled;
            app_ui_update();
            return 0;
        case IDC_FN_LOCK_RADIO:
            settings_draft_fn_locked = 1;
            app_ui_update();
            return 0;
        case IDC_FN_UNLOCK_RADIO:
            settings_draft_fn_locked = 0;
            app_ui_update();
            return 0;
        case IDC_TRAY_CHECKBOX:
            settings_draft_tray_visible = !settings_draft_tray_visible;
            app_ui_update();
            return 0;
        case IDC_OK_BUTTON:
            commit_settings(window);
            return 0;
        case IDC_CLOSE_BUTTON:
            if (MessageBoxW(window,
                            L"确定要退出程序吗？退出后，Fn 设置将不再生效。",
                            L"K380 Fn Auto Lock",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES &&
                g_app.watcher_window != NULL)
                PostMessageW(g_app.watcher_window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (settings_have_unsaved_changes()) {
            int choice = MessageBoxW(window,
                                     L"有未保存的设置。是否保存更改？",
                                     L"K380 Fn 设置",
                                     MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (choice == IDYES) {
                if (commit_settings(window))
                    close_settings_and_restart(window);
                return 0;
            }
            if (choice != IDNO)
                return 0;
        }
        close_settings_and_restart(window);
        return 0;
    case WM_DESTROY:
        settings_draft_valid = 0;
        settings_title_label = NULL;
        if (settings_title_font != NULL)
            DeleteObject(settings_title_font);
        settings_title_font = NULL;
        if (g_app.owns_settings_font && g_app.settings_font != NULL)
            DeleteObject(g_app.settings_font);
        g_app.settings_font = NULL;
        g_app.owns_settings_font = 0;
        g_app.startup_checkbox = NULL;
        g_app.fn_lock_radio = NULL;
        g_app.fn_unlock_radio = NULL;
        g_app.tray_checkbox = NULL;
        g_app.status_label = NULL;
        g_app.settings_window = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}


int app_ui_register(HINSTANCE instance)
{
    WNDCLASSW definition = {0};
    definition.lpfnWndProc = settings_window_proc;
    definition.hInstance = instance;
    definition.lpszClassName = settings_window_class;
    return RegisterClassW(&definition) != 0;
}

void app_ui_set_tray_visibility(int visible)
{
    NOTIFYICONDATAW icon = {0};
    if (visible) {
        app_ui_add_tray_icon(g_app.watcher_window);
    } else if (g_app.tray_icon_added) {
        icon.cbSize = sizeof(icon);
        icon.hWnd = g_app.watcher_window;
        icon.uID = TRAY_ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        g_app.tray_icon_added = 0;
    }
    app_ui_update();
}

void app_ui_on_taskbar_created(void)
{
    g_app.tray_icon_added = 0;
    app_ui_add_tray_icon(g_app.watcher_window);
}

void app_ui_handle_tray_callback(WPARAM wparam, LPARAM lparam)
{
    if ((UINT)wparam != TRAY_ICON_ID)
        return;
    if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)
        show_tray_menu(g_app.watcher_window);
    else if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK)
        app_ui_show_settings();
}

void app_ui_unregister(HINSTANCE instance)
{
    NOTIFYICONDATAW icon = {0};
    if (g_app.tray_icon_added) {
        icon.cbSize = sizeof(icon);
        icon.hWnd = g_app.watcher_window;
        icon.uID = TRAY_ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        g_app.tray_icon_added = 0;
    }
    if (g_app.settings_window != NULL)
        DestroyWindow(g_app.settings_window);
    UnregisterClassW(settings_window_class, instance);
}
