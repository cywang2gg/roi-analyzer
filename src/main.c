// main.c — WinMain, window/menu, message dispatch. No timers, no threads.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus/gdiplus.h>
#include "image.h"
#include "view.h"
#include "roi.h"
#include "analyze.h"
#include "log.h"

#define IDM_OPEN   101
#define IDM_EXIT   102
#define IDM_DRAG   111
#define IDM_FIX3   112
#define IDM_FIX5   113
#define IDM_FOLDER 121
#define IDM_CLEAR  122
#define IDM_OPENLOG 123

static HWND g_hwnd, g_status;
static image_t g_img;
static view_t g_view;
static roi_state_t g_roi;
static roi_result_t g_last;
static BOOL g_has_last = FALSE;
static ULONG_PTR g_gdiplus = 0;

static void UpdateTitle(void) {
    char t[512];
    if (g_img.valid)
        _snprintf(t, sizeof(t), "ROI Analyzer - %s %dx%d [%s]",
                  g_img.path[0] ? g_img.path : "?",
                  g_img.w, g_img.h, ROI_ModeTitle(g_roi.mode));
    else
        _snprintf(t, sizeof(t), "ROI Analyzer - (drop an image) [%s]",
                  ROI_ModeTitle(g_roi.mode));
    t[sizeof(t) - 1] = '\0';
    // Title shows file name only (not full path) + dims.
    if (g_img.valid) {
        const char *b = strrchr(g_img.path, '\\');
        const char *b2 = strrchr(g_img.path, '/');
        if (b2 && (!b || b2 > b)) b = b2;
        b = b ? b + 1 : g_img.path;
        _snprintf(t, sizeof(t), "ROI Analyzer - %s %dx%d [%s]",
                  b, g_img.w, g_img.h, ROI_ModeTitle(g_roi.mode));
        t[sizeof(t) - 1] = '\0';
    }
    SetWindowTextA(g_hwnd, t);
}

static void UpdateStatus(const char *mouse) {
    char s[512];
    if (g_has_last)
        _snprintf(s, sizeof(s), "%s   |   R=%.2f G=%.2f B=%.2f Y=%.2f (%s rect=(%d,%d)-(%d,%d) n=%d)",
                  mouse ? mouse : "",
                  g_last.r_mean, g_last.g_mean, g_last.b_mean, g_last.y_mean,
                  ROI_ModeStr(g_roi.mode), g_last.x0, g_last.y0, g_last.x1, g_last.y1,
                  g_last.count);
    else
        _snprintf(s, sizeof(s), "%s", mouse ? mouse : "drop PNG/JPG/BMP here | 1=Drag 2=3x3 3=5x5 C=clear O=open | confirm appends the image log");
    s[sizeof(s) - 1] = '\0';
    SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM)s);
}

static void RefreshView(void) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int sh = 0;
    if (g_status) {
        RECT sr;
        GetWindowRect(g_status, &sr);
        sh = sr.bottom - sr.top;
    }
    View_Update(&g_view, rc.right, rc.bottom - sh,
                g_img.valid ? g_img.w : 0, g_img.valid ? g_img.h : 0);
}

static void SetMode(roi_mode_t m) {
    g_roi.mode = m;
    g_roi.dragging = FALSE;
    g_roi.has_preview = FALSE;
    if (m != MODE_DRAG && g_img.valid) {
        g_roi.preview.x = g_img.w / 2;
        g_roi.preview.y = g_img.h / 2;
        g_roi.has_preview = TRUE;
    }
    CheckMenuItem(GetMenu(g_hwnd), IDM_DRAG, MF_BYCOMMAND | (m == MODE_DRAG ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hwnd), IDM_FIX3, MF_BYCOMMAND | (m == MODE_FIX3 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hwnd), IDM_FIX5, MF_BYCOMMAND | (m == MODE_FIX5 ? MF_CHECKED : MF_UNCHECKED));
    UpdateTitle();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

// Analyze + log one image-coord rect; updates status/title/paint.
static void ConfirmROI(RECT img_rc) {
    AnalyzeROI(&g_img, img_rc, &g_last);
    if (g_last.count <= 0) return;
    g_has_last = TRUE;
    if (LogROI(g_img.path, ROI_ModeStr(g_roi.mode), &g_last) != 0)
        MessageBoxA(g_hwnd, "Failed to write the current image log.", "Log", MB_OK | MB_ICONWARNING);
    UpdateStatus(NULL);
    UpdateTitle();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void OpenImageFile(const char *path) {
    if (!path || !path[0]) return;
    image_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (Image_Load(&tmp, path) != 0) {
        MessageBoxA(g_hwnd, "Cannot load image (PNG/JPG/BMP only).", "Open", MB_OK | MB_ICONWARNING);
        return;
    }
    Image_Free(&g_img);
    g_img = tmp; // struct copy transfers buffer ownership
    ROI_Clear(&g_roi);
    g_has_last = FALSE;
    RefreshView();
    UpdateTitle();
    UpdateStatus(NULL);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void OpenImageDialog(void) {
    char f[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All\0*.*\0";
    ofn.lpstrFile = f;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) OpenImageFile(f);
}

static void OpenLogFolder(void) {
    char mod[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, mod, MAX_PATH);
    char *s = strrchr(mod, '\\');
    if (!s) s = strrchr(mod, '/');
    if (s) *s = '\0';
    ShellExecuteA(NULL, "open", mod[0] ? mod : ".", NULL, NULL, SW_SHOWNORMAL);
}

// Log menu: open the current image's auto-written log directly.
static void OpenLogFile(void) {
    char path[MAX_PATH] = { 0 };
    if (g_img.valid)
        Log_GetPath(g_img.path, ROI_ModeStr(g_roi.mode), path, sizeof(path));
    if (path[0] && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    MessageBoxA(g_hwnd,
                g_img.valid ? "The current image has no log yet.\nConfirm an ROI first."
                            : "No current image.\nOpen an image and confirm an ROI first.",
                "Log", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_status = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                                   WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                   0, 0, 0, 0, hwnd, (HMENU)1000,
                                   GetModuleHandle(NULL), NULL);
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    }
    case WM_SIZE:
        if (g_status) SendMessage(g_status, WM_SIZE, 0, 0);
        RefreshView();
        UpdateStatus(NULL);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDM_OPEN) OpenImageDialog();
        else if (id == IDM_EXIT) DestroyWindow(hwnd);
        else if (id == IDM_DRAG) SetMode(MODE_DRAG);
        else if (id == IDM_FIX3) SetMode(MODE_FIX3);
        else if (id == IDM_FIX5) SetMode(MODE_FIX5);
        else if (id == IDM_FOLDER) OpenLogFolder();
        else if (id == IDM_OPENLOG) OpenLogFile();
        else if (id == IDM_CLEAR) {
            if (g_img.valid)
                Log_Clear(g_img.path, ROI_ModeStr(g_roi.mode));
            ROI_Clear(&g_roi);
            g_has_last = FALSE;
            UpdateStatus("log cleared");
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == '1') SetMode(MODE_DRAG);
        else if (wp == '2') SetMode(MODE_FIX3);
        else if (wp == '3') SetMode(MODE_FIX5);
        else if (wp == 'C') {
            ROI_Clear(&g_roi);
            g_has_last = FALSE;
            UpdateStatus(NULL);
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (wp == 'O') OpenImageDialog();
        return 0;
    case WM_LBUTTONDOWN: {
        if (!g_img.valid) return 0;
        POINT p; p.x = LOWORD(lp); p.y = HIWORD(lp);
        RECT rc;
        SetCapture(hwnd);
        if (ROI_OnLDown(&g_roi, &g_view, &g_img, p, &rc)) {
            ReleaseCapture();
            ConfirmROI(rc);
        } else {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_img.valid) return 0;
        POINT p; p.x = LOWORD(lp); p.y = HIWORD(lp);
        char m[64] = "";
        POINT ip;
        if (View_ToImage(&g_view, g_img.w, g_img.h, p, &ip)) {
            const unsigned char *px = g_img.px + (size_t)ip.y * (size_t)g_img.pitch;
            _snprintf(m, sizeof(m), "(%d,%d) #%02X%02X%02X",
                      (int)ip.x, (int)ip.y, px[ip.x * 4 + 2], px[ip.x * 4 + 1], px[ip.x * 4 + 0]);
            m[sizeof(m) - 1] = '\0';
        }
        if (ROI_OnMove(&g_roi, &g_view, &g_img, p)) {
            /* Full-client repaint without erase: mem-DC double buffer +
               WM_ERASEBKGND skip means no flicker, and the old box
               pixels are always restored. Per-region invalidation left
               ghost corners (verified on-screen), so keep it simple. */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        UpdateStatus(m[0] ? m : NULL);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!g_img.valid) return 0;
        POINT p; p.x = LOWORD(lp); p.y = HIWORD(lp);
        RECT rc;
        if (ROI_OnLUp(&g_roi, &g_view, &g_img, p, &rc)) ConfirmROI(rc);
        ReleaseCapture();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        char f[MAX_PATH] = { 0 };
        if (DragQueryFileA(hd, 0, f, MAX_PATH)) OpenImageFile(f);
        DragFinish(hd);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; /* we paint the full client area from the mem-DC; skip erase to avoid flicker */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int sh = 0;
        if (g_status) { RECT sr; GetWindowRect(g_status, &sr); sh = sr.bottom - sr.top; }
        rc.bottom -= sh;
        // Paint area: (0,0)-(w,h) in window coords, excludes status bar.
        int pw = rc.right - rc.left;
        int ph = rc.bottom - rc.top;
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, pw > 0 ? pw : 1, ph > 0 ? ph : 1);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        RECT mem_rc;
        mem_rc.left = 0; mem_rc.top = 0; mem_rc.right = pw; mem_rc.bottom = ph;
        FillRect(mem, &mem_rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (g_img.valid) {
            // g_view offsets are relative to this same (0,0)-(w,h) origin,
            // so no off_y correction is needed: draw + overlay convert
            // image coords -> window coords via the current view directly.
            View_DrawImage(mem, &g_view, &g_img);
            ROI_DrawOverlay(mem, &g_view, &g_roi);
        }
        BitBlt(hdc, rc.left, rc.top, pw, ph, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd;
    GdiplusStartupInput gsi = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&g_gdiplus, &gsi, NULL) != 0) {
        MessageBoxA(NULL, "GDI+ init failed.", "ROI Analyzer", MB_OK | MB_ICONERROR);
        return 1;
    }
    memset(&g_img, 0, sizeof(g_img));
    memset(&g_view, 0, sizeof(g_view));
    memset(&g_last, 0, sizeof(g_last));
    ROI_Init(&g_roi);

    InitCommonControls();
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "RoiAnalyzerWnd";
    RegisterClassA(&wc);

    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_OPEN, "Open...\tO");
    AppendMenuA(file, MF_STRING, IDM_EXIT, "Exit");
    HMENU mode = CreatePopupMenu();
    AppendMenuA(mode, MF_STRING | MF_CHECKED, IDM_DRAG, "Drag\t1");
    AppendMenuA(mode, MF_STRING, IDM_FIX3, "3x3\t2");
    AppendMenuA(mode, MF_STRING, IDM_FIX5, "5x5\t3");
    HMENU logm = CreatePopupMenu();
    AppendMenuA(logm, MF_STRING, IDM_OPENLOG, "Open Log File");
    AppendMenuA(logm, MF_STRING, IDM_FOLDER, "Open Folder");
    AppendMenuA(logm, MF_STRING, IDM_CLEAR, "Clear");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)mode, "Mode");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)logm, "Log");

    // Centered half-screen default (primary monitor): x=w/4, y=h/4, w/2 x h/2.
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int ww = (sw > 0) ? sw / 2 : 1280;
    int wh = (sy > 0) ? sy / 2 : 800;
    int wx = (sw > 0) ? sw / 4 : CW_USEDEFAULT;
    int wy = (sy > 0) ? sy / 4 : CW_USEDEFAULT;
    g_hwnd = CreateWindowExA(WS_EX_ACCEPTFILES, "RoiAnalyzerWnd", "ROI Analyzer",
                             WS_OVERLAPPEDWINDOW, wx, wy,
                             ww, wh, NULL, bar, hi, NULL);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);
    UpdateTitle();
    UpdateStatus(NULL);

    // Optional CLI arg: image path to open at startup (headless-verifiable).
    // WinMain's cmd holds the raw command tail; strip one pair of quotes.
    if (cmd && cmd[0]) {
        char cli_path[MAX_PATH];
        size_t cli_len = strlen(cmd);

        strncpy(cli_path, cmd, MAX_PATH - 1);
        cli_path[MAX_PATH - 1] = '\0';
        if (cli_len >= 2 && cli_path[0] == '"' && cli_path[cli_len - 1] == '"') {
            cli_path[cli_len - 1] = '\0';
            memmove(cli_path, cli_path + 1, cli_len - 1);
        }
        OpenImageFile(cli_path);
    }

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    Image_Free(&g_img);
    GdiplusShutdown(g_gdiplus);
    return (int)m.wParam;
}
