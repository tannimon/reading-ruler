// Reading Ruler - a click-through dimming overlay for Windows.
//
// One layered, topmost, non-activating window covers the whole virtual desktop
// and is filled flat black at a uniform alpha. The clear reading band is not
// painted at all: it is a hole cut out of the window region, so it costs
// nothing to move and passes input straight through by definition.
//
//   Ctrl+Alt+R  on / off        Ctrl+Alt+Up/Down  band height
//   Ctrl+Alt+L  lock in place   Ctrl+Alt+[ / ]    dim level
//   Ctrl+Alt+Q  quit
//
// Build:  see build_mingw.bat or build_msvc.bat
// MIT licensed. (c) 2026 Tanni Monroe

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#include "resource.h"

// ------------------------------------------------------------------ config --
static const int   BAND_DEFAULT = 72;
static const int   BAND_MIN     = 24;
static const int   BAND_MAX     = 400;
static const int   BAND_STEP    = 8;

static const double DIM_DEFAULT = 0.65;
static const double DIM_MIN     = 0.10;
static const double DIM_MAX     = 0.92;
static const double DIM_STEP    = 0.05;

// Band width. 0 means the full width of the screen.
static const int   WIDTH_MIN    = 240;
static const int   WIDTH_STEP   = 80;

static const UINT  POLL_MS      = 15;
static const bool  EDGE_LINES   = true;

// ------------------------------------------------------------------- state --
static const wchar_t* CLASS_NAME = L"ReadingRulerOverlay";

enum { HK_TOGGLE = 1, HK_LOCK, HK_TALLER, HK_SHORTER, HK_DIMMER, HK_BRIGHTER,
       HK_NARROWER, HK_WIDER, HK_FULLWIDTH, HK_QUIT };
enum { TIMER_POLL = 1, TIMER_STATUS, TIMER_TOPMOST };
enum { WM_TRAY = WM_APP + 1, IDM_TOGGLE = 100, IDM_LOCK, IDM_QUIT };

static HINSTANCE g_inst      = NULL;
static HWND      g_hwnd      = NULL;
static NOTIFYICONDATAW g_nid = {};
static HFONT     g_font      = NULL;

static bool   g_enabled  = true;
static bool   g_locked   = false;
static int    g_bandH    = BAND_DEFAULT;
static int    g_bandW    = 0;              // 0 = full screen width
static double g_dim      = DIM_DEFAULT;

static int g_vx = 0, g_vy = 0, g_vw = 0, g_vh = 0;   // virtual desktop bounds
static RECT g_band    = { 0, 0, 0, 0 };               // window-relative
static int g_lockedX  = 0, g_lockedY = 0;
static int g_lastX    = INT_MIN, g_lastY = INT_MIN;

static wchar_t g_status[128] = L"";
static bool    g_statusOn    = false;

// Fallback input path, used only for shortcuts RegisterHotKey refused.
static HHOOK g_hook = NULL;
static bool  g_hookNeeded[16] = { false };

struct KeyDef { int id; UINT vk; const wchar_t* name; };
static const KeyDef KEYS[] = {
    { HK_TOGGLE,    'R',      L"Ctrl+Alt+R" },
    { HK_LOCK,      'L',      L"Ctrl+Alt+L" },
    { HK_TALLER,    VK_UP,    L"Ctrl+Alt+Up" },
    { HK_SHORTER,   VK_DOWN,  L"Ctrl+Alt+Down" },
    { HK_DIMMER,    VK_OEM_6, L"Ctrl+Alt+]" },
    { HK_BRIGHTER,  VK_OEM_4, L"Ctrl+Alt+[" },
    { HK_NARROWER,  VK_LEFT,  L"Ctrl+Alt+Left" },
    { HK_WIDER,     VK_RIGHT, L"Ctrl+Alt+Right" },
    { HK_FULLWIDTH, 'F',      L"Ctrl+Alt+F" },
    { HK_QUIT,      'Q',      L"Ctrl+Alt+Q" },
};
static const int KEY_COUNT = (int)(sizeof(KEYS) / sizeof(KEYS[0]));

// Written at startup so a failure can be diagnosed after the fact.
static void Log(const wchar_t* fmt, ...);

// --------------------------------------------------------------- settings --
static bool SettingsPath(wchar_t* out, size_t n)
{
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return false;
    _snwprintf(out, n, L"%ls\\ReadingRuler", appdata);
    CreateDirectoryW(out, NULL);
    _snwprintf(out, n, L"%ls\\ReadingRuler\\settings.ini", appdata);
    return true;
}

static void LoadSettings(void)
{
    wchar_t path[MAX_PATH];
    if (!SettingsPath(path, MAX_PATH)) return;
    FILE* f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) return;
    int band = BAND_DEFAULT;
    int width = 0;
    double dim = DIM_DEFAULT;
    wchar_t line[128];
    while (fgetws(line, 128, f)) {
        if (swscanf(line, L"band=%d", &band) == 1) continue;
        if (swscanf(line, L"width=%d", &width) == 1) continue;
        swscanf(line, L"dim=%lf", &dim);
    }
    fclose(f);
    if (band < BAND_MIN) band = BAND_MIN;
    if (band > BAND_MAX) band = BAND_MAX;
    if (dim < DIM_MIN)   dim  = DIM_MIN;
    if (dim > DIM_MAX)   dim  = DIM_MAX;
    if (width && width < WIDTH_MIN) width = WIDTH_MIN;
    g_bandH = band;
    g_bandW = width;
    g_dim   = dim;
}

static void SaveSettings(void)
{
    wchar_t path[MAX_PATH];
    if (!SettingsPath(path, MAX_PATH)) return;
    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"band=%d\nwidth=%d\ndim=%.3f\n", g_bandH, g_bandW, g_dim);
    fclose(f);
}

// ------------------------------------------------------------------- log --
static void Log(const wchar_t* fmt, ...)
{
    wchar_t appdata[MAX_PATH], path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return;
    _snwprintf(path, MAX_PATH, L"%ls\\ReadingRuler", appdata);
    CreateDirectoryW(path, NULL);
    _snwprintf(path, MAX_PATH, L"%ls\\ReadingRuler\\log.txt", appdata);

    FILE* f = _wfopen(path, L"a, ccs=UTF-8");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fwprintf(f, L"\n");
    fclose(f);
}

// ------------------------------------------------------------- key hook --
// Only installed when RegisterHotKey was refused, and it forwards only the
// specific combinations that failed. Every other keystroke passes through
// untouched.
static LRESULT CALLBACK KeyHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        if (ctrl && alt) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (KEYS[i].vk == k->vkCode && g_hookNeeded[KEYS[i].id]) {
                    PostMessageW(g_hwnd, WM_HOTKEY, KEYS[i].id, 0);
                    return 1;   // swallow, so the other program does not act too
                }
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

// ------------------------------------------------------------------ visual --
static BYTE DimAlpha(void) { return (BYTE)(g_dim * 255.0 + 0.5); }

static void ApplyAlpha(void)
{
    SetLayeredWindowAttributes(g_hwnd, 0, DimAlpha(), LWA_ALPHA);
}

// Cut the reading band out of the window region. The hole is genuinely absent
// from the window, so it is both perfectly clear and click-through.
static void ApplyBand(int centreX, int centreY)
{
    int top = centreY - g_bandH / 2;
    int left, right;

    if (g_bandW <= 0 || g_bandW >= g_vw) {
        left  = 0;
        right = g_vw;
    } else {
        // A narrowed band is a reading window that tracks the cursor
        // horizontally as well, clamped to the desktop.
        left = centreX - g_bandW / 2;
        if (left < 0) left = 0;
        if (left + g_bandW > g_vw) left = g_vw - g_bandW;
        right = left + g_bandW;
    }

    g_band.left = left;  g_band.top    = top;
    g_band.right = right; g_band.bottom = top + g_bandH;

    HRGN full = CreateRectRgn(0, 0, g_vw, g_vh);
    HRGN hole = CreateRectRgn(left, top, right, top + g_bandH);
    CombineRgn(full, full, hole, RGN_DIFF);
    DeleteObject(hole);

    SetWindowRgn(g_hwnd, full, TRUE);   // window owns the region now
}

static void ShowStatus(const wchar_t* text, UINT ms = 1100)
{
    wcsncpy(g_status, text, 127);
    g_status[127] = 0;
    g_statusOn = true;
    SetTimer(g_hwnd, TIMER_STATUS, ms, NULL);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void UpdateGeometry(void)
{
    g_vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(g_hwnd, HWND_TOPMOST, g_vx, g_vy, g_vw, g_vh, SWP_NOACTIVATE);
    g_lastX = g_lastY = INT_MIN;
}

// -------------------------------------------------------------------- tray --
static void TrayAdd(void)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = g_hwnd;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon  = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON),
                                     GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"Reading Ruler  -  right-click to quit");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Windows hides new tray icons under the chevron, so say where it went.
    NOTIFYICONDATAW b = g_nid;
    b.uFlags = NIF_INFO;
    wcscpy(b.szInfoTitle, L"Reading Ruler is running");
    wcscpy(b.szInfo, L"To quit: right-click this icon and choose Quit. "
                     L"It may be under the ^ arrow beside your clock.");
    b.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &b);
}

static void TrayMenu(void)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TOGGLE, g_enabled ? L"Turn ruler off\tCtrl+Alt+R"
                                                    : L"Turn ruler on\tCtrl+Alt+R");
    AppendMenuW(m, MF_STRING, IDM_LOCK, g_locked ? L"Unlock\tCtrl+Alt+L"
                                                 : L"Lock in place\tCtrl+Alt+L");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quit\tCtrl+Alt+Q");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);          // so the menu dismisses properly
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

// ---------------------------------------------------------------- commands --
static void ToggleRuler(void)
{
    g_enabled = !g_enabled;
    if (g_enabled) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_lastX = g_lastY = INT_MIN;
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
    }
}

static void ToggleLock(void)
{
    g_locked = !g_locked;
    if (g_locked) {
        POINT pt;
        GetCursorPos(&pt);
        g_lockedX = pt.x - g_vx;
        g_lockedY = pt.y - g_vy;
    }
    ShowStatus(g_locked ? L"Locked" : L"Unlocked");
}

static void NudgeBand(int delta)
{
    g_bandH += delta;
    if (g_bandH < BAND_MIN) g_bandH = BAND_MIN;
    if (g_bandH > BAND_MAX) g_bandH = BAND_MAX;
    g_lastX = g_lastY = INT_MIN;
    wchar_t buf[64];
    _snwprintf(buf, 64, L"Height  %d px", g_bandH);
    ShowStatus(buf);
    SaveSettings();
}

static void NudgeWidth(int delta)
{
    // Coming down from full width, start at the screen width so the first
    // press actually narrows rather than jumping.
    if (g_bandW <= 0) g_bandW = g_vw;

    g_bandW += delta;

    wchar_t buf[64];
    if (g_bandW >= g_vw) {
        g_bandW = 0;
        wcscpy(buf, L"Width  full screen");
    } else {
        if (g_bandW < WIDTH_MIN) g_bandW = WIDTH_MIN;
        _snwprintf(buf, 64, L"Width  %d px", g_bandW);
    }

    g_lastX = g_lastY = INT_MIN;
    ShowStatus(buf);
    SaveSettings();
}

static void SetFullWidth(void)
{
    g_bandW = 0;
    g_lastX = g_lastY = INT_MIN;
    ShowStatus(L"Width  full screen");
    SaveSettings();
}

static void NudgeDim(double delta)
{
    g_dim += delta;
    if (g_dim < DIM_MIN) g_dim = DIM_MIN;
    if (g_dim > DIM_MAX) g_dim = DIM_MAX;
    ApplyAlpha();
    wchar_t buf[64];
    _snwprintf(buf, 64, L"Dim  %d%%", (int)(g_dim * 100 + 0.5));
    ShowStatus(buf);
    SaveSettings();
}

// ------------------------------------------------------------------ window --
static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &ps.rcPaint, black);
    DeleteObject(black);

    if (EDGE_LINES) {
        HBRUSH edge = CreateSolidBrush(RGB(150, 140, 175));
        RECT fr = { g_band.left - 1, g_band.top - 1,
                    g_band.right + 1, g_band.bottom + 1 };
        FrameRect(dc, &fr, edge);
        DeleteObject(edge);
    }

    if (g_statusOn && g_status[0]) {
        HFONT old = (HFONT)SelectObject(dc, g_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 236, 255));
        RECT r = { 0, g_vh - 96, g_vw, g_vh - 56 };
        DrawTextW(dc, g_status, -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, old);
    }

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_POLL) {
            if (!g_enabled) return 0;
            int x, y;
            if (g_locked) {
                x = g_lockedX;
                y = g_lockedY;
            } else {
                POINT pt;
                GetCursorPos(&pt);
                x = pt.x - g_vx;
                y = pt.y - g_vy;
            }
            // Horizontal movement only matters once the band is narrowed.
            if (y != g_lastY || (g_bandW > 0 && x != g_lastX)) {
                g_lastX = x;
                g_lastY = y;
                ApplyBand(x, y);
            }
        } else if (wp == TIMER_STATUS) {
            KillTimer(hwnd, TIMER_STATUS);
            g_statusOn = false;
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == TIMER_TOPMOST && g_enabled) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;

    case WM_HOTKEY:
        switch (wp) {
        case HK_TOGGLE:   ToggleRuler();        break;
        case HK_LOCK:     if (g_enabled) ToggleLock();          break;
        case HK_TALLER:   if (g_enabled) NudgeBand(BAND_STEP);  break;
        case HK_SHORTER:  if (g_enabled) NudgeBand(-BAND_STEP); break;
        case HK_DIMMER:   if (g_enabled) NudgeDim(DIM_STEP);    break;
        case HK_BRIGHTER: if (g_enabled) NudgeDim(-DIM_STEP);   break;
        case HK_NARROWER: if (g_enabled) NudgeWidth(-WIDTH_STEP); break;
        case HK_WIDER:    if (g_enabled) NudgeWidth(WIDTH_STEP);  break;
        case HK_FULLWIDTH:if (g_enabled) SetFullWidth();          break;
        case HK_QUIT:     DestroyWindow(hwnd);  break;
        }
        return 0;

    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP)      ToggleRuler();
        else if (LOWORD(lp) == WM_RBUTTONUP) TrayMenu();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE: ToggleRuler();       break;
        case IDM_LOCK:   ToggleLock();        break;
        case IDM_QUIT:   DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        UpdateGeometry();
        return 0;

    case WM_DESTROY:
        SaveSettings();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        for (int i = HK_TOGGLE; i <= HK_QUIT; ++i) UnregisterHotKey(hwnd, i);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// -------------------------------------------------------------------- main --
static void EnableDpiAwareness(void)
{
    // Per-monitor v2 where available, plain awareness on older builds.
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtxFn fn = u ? (SetCtxFn)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (fn && fn((HANDLE)-4)) return;   // PER_MONITOR_AWARE_V2
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    g_inst = inst;

    // One instance only - two overlays would double the dimming.
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"ReadingRulerSingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Already running. Double-clicking the exe again is then the exit that
        // needs no shortcut and no tray icon.
        HWND prev = FindWindowW(CLASS_NAME, NULL);
        int r = MessageBoxW(NULL,
                    L"Reading Ruler is already running.\n\nQuit it now?",
                    L"Reading Ruler",
                    MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
        if (r == IDYES && prev) PostMessageW(prev, WM_CLOSE, 0, 0);
        return 0;
    }

    EnableDpiAwareness();
    LoadSettings();

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    g_vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        CLASS_NAME, L"Reading Ruler", WS_POPUP,
        g_vx, g_vy, g_vw, g_vh,
        NULL, NULL, inst, NULL);

    if (!g_hwnd) return 1;

    g_font = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    ApplyAlpha();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    POINT pt;
    GetCursorPos(&pt);
    ApplyBand(pt.x - g_vx, pt.y - g_vy);

    TrayAdd();

    const UINT mods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;

    int failedCount = 0;
    wchar_t failed[512] = L"";
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (RegisterHotKey(g_hwnd, KEYS[i].id, mods, KEYS[i].vk)) {
            Log(L"registered %ls", KEYS[i].name);
        } else {
            g_hookNeeded[KEYS[i].id] = true;
            ++failedCount;
            if (failed[0]) wcscat(failed, L", ");
            wcscat(failed, KEYS[i].name);
            Log(L"REFUSED %ls (error %lu)", KEYS[i].name, GetLastError());
        }
    }

    if (failedCount) {
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, inst, 0);
        Log(L"fallback key hook: %ls", g_hook ? L"installed" : L"FAILED");
    }

    SetTimer(g_hwnd, TIMER_POLL, POLL_MS, NULL);
    SetTimer(g_hwnd, TIMER_TOPMOST, 2000, NULL);

    if (failedCount && !g_hook) {
        // The only case where the shortcuts genuinely will not work. Say so
        // loudly, above the overlay, before it has a chance to confuse anyone.
        wchar_t msg[900];
        _snwprintf(msg, 900,
            L"Another program has already claimed these shortcuts:\n\n%ls\n\n"
            L"To quit Reading Ruler: right-click its icon beside the clock "
            L"(you may need the ^ arrow to see it) and choose Quit.\n\n"
            L"Running ReadingRuler.exe again also offers to quit it.", failed);
        MessageBoxW(NULL, msg, L"Reading Ruler",
                    MB_ICONWARNING | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
    }

    ShowStatus(L"Reading Ruler on   \u00b7   Ctrl+Alt+R toggles   \u00b7   "
               L"Ctrl+Alt+Q quits   \u00b7   or use the tray icon", 6000);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_hook) UnhookWindowsHookEx(g_hook);
    if (g_font) DeleteObject(g_font);
    if (mutex) CloseHandle(mutex);
    return 0;
}
