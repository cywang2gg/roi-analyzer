#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus/gdiplus.h>
#include <limits.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "app.h"
#include "canvas.h"
#include "export.h"
#include "histpanel.h"
#include "table.h"

#define IDM_OPEN       101
#define IDM_EXPORT     102
#define IDM_EXIT       103
#define IDM_PREVIOUS   104
#define IDM_NEXT       105
#define IDM_DRAG       111
#define IDM_GRID3      112
#define IDM_GRID5      113
#define IDM_MULTI      114
#define IDM_DELETE     121
#define IDM_CLEAR      122
#define IDM_CLEAR_ALL  123
#define IDM_OPENLOG    131
#define IDM_FOLDER     132
#define IDM_ZOOM_IN    141
#define IDM_ZOOM_OUT   142
#define IDM_HISTOGRAM  151
#define IDM_HIST_RGB   152
#define IDM_HIST_Y     153
#define IDM_HIST_R     154
#define IDM_HIST_G     155
#define IDM_HIST_B     156
#define IDM_HIST_LOG   157
#define IDC_CANVAS     1001
#define IDC_TABLE      1002
#define IDC_EXPORT     1003
#define IDC_CLEAR      1004
#define IDC_MULTI      1005
#define IDC_TABS       1006
#define IDC_HISTPANEL  1007
#define SB_PART_POS    0
#define SB_PART_MSG    1
#define SB_PART_MODE   2
#define SB_PART_INDEX  3
#define SB_PART_TIME   4
#define SB_PART_COUNT  5

app_t g_app;

static ULONG_PTR g_gdiplus;
static HACCEL g_accelerators;
static BOOL g_syncing_table;
static BOOL g_main_wm_create_started;
static HMENU g_menu_mode;
static HMENU g_menu_view;
static hist_channel_t g_hist_channel = HCH_RGB;
static BOOL g_hist_log;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_nav_started;
static double g_load_ms;
static double g_analyze_ms;
static double g_hist_ms;
static double g_show_ms;
static double g_done_ms;
static BOOL g_current_exact;
static BOOL g_browsing;
static char g_nav_status[128] = "Ready";
static char g_status_index[32];

static BOOL App_InitCommonControls(void);
static void Layout(void);
static void SetMode(roi_mode_t mode);
static void SetMulti(BOOL multi);
static void ExportCurrent(void);
static void ChangeZoom(float factor);
static void SetHistogramChannel(hist_channel_t channel);
static void UpdateTitle(void);
static void App_UpdateTable(void);

static BOOL copy_utf8_to_acp(char *destination, size_t capacity,
                             const char *source)
{
    wchar_t wide[128];
    if (!destination || !source || capacity == 0 || capacity > INT_MAX)
        return FALSE;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, wide,
                            (int)(sizeof(wide) / sizeof(wide[0]))) <= 0)
        return FALSE;
    return WideCharToMultiByte(CP_ACP, 0, wide, -1, destination,
                               (int)capacity, NULL, NULL) > 0;
}

static BOOL App_InitCommonControls(void)
{
    INITCOMMONCONTROLSEX icc;

    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    if (InitCommonControlsEx(&icc))
        return TRUE;

    {
        DWORD error = GetLastError();
        char message[160];
        InitCommonControls();
        snprintf(message, sizeof(message),
                 "InitCommonControlsEx failed (GetLastError=%lu); "
                 "attempted InitCommonControls fallback.\n",
                 (unsigned long)error);
        OutputDebugStringA(message);
    }
    return FALSE;
}

static void ReportCreateWindowFailureA(const char *control)
{
    char message[160];
    DWORD error = GetLastError();
    snprintf(message, sizeof(message), "Create %s failed (GetLastError=%lu).",
             control, (unsigned long)error);
    MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
}

static const char *image_basename(const char *path)
{
    const char *base = path;
    const char *slash = strrchr(path, '\\');
    const char *forward = strrchr(path, '/');
    if (slash && slash + 1 > base)
        base = slash + 1;
    if (forward && forward + 1 > base)
        base = forward + 1;
    return base;
}

static void UpdateTitle(void)
{
    char title[MAX_PATH + 128];
    char dash[8] = "-";
    if (!copy_utf8_to_acp(dash, sizeof(dash), "—"))
        OutputDebugStringA("ROI Analyzer: could not convert the title dash to ANSI.\n");
    if (g_app.img.valid) {
        if (g_current_exact)
            _snprintf(title, sizeof(title),
                      "ROI Analyzer - %s %dx%d [%d/%d] [%s] [%s]",
                      image_basename(g_app.img.path), g_app.img.w, g_app.img.h,
                      g_app.file_idx + 1, g_app.files.count,
                      ROI_ModeLabel(g_app.mode), g_app.multi ? "Multi" : "Single");
        else
            _snprintf(title, sizeof(title),
                      "ROI Analyzer - %s %dx%d [%s/%d] [%s] [%s]",
                      image_basename(g_app.img.path), g_app.img.w, g_app.img.h,
                      dash, g_app.files.count, ROI_ModeLabel(g_app.mode),
                      g_app.multi ? "Multi" : "Single");
    } else {
        _snprintf(title, sizeof(title), "ROI Analyzer - (open or drop an image) [%s] [%s]",
                  ROI_ModeLabel(g_app.mode), g_app.multi ? "Multi" : "Single");
    }
    title[sizeof(title) - 1] = '\0';
    SetWindowTextA(g_app.hwnd_main, title);
}

double App_Ms(LARGE_INTEGER start)
{
    LARGE_INTEGER now;
    if (!g_qpc_frequency.QuadPart || !QueryPerformanceCounter(&now))
        return 0.0;
    return (double)(now.QuadPart - start.QuadPart) * 1000.0 /
           (double)g_qpc_frequency.QuadPart;
}

void App_UpdateStatus(void)
{
    char cursor[128] = "cursor: outside image";
    char mode[192];
    char time[128];
    char browsing[32];
    const char *message;
    POINT screen, client, image_point;

    if (!g_app.hwnd_status)
        return;
    if (GetCursorPos(&screen) && g_app.hwnd_canvas) {
        client = screen;
        ScreenToClient(g_app.hwnd_canvas, &client);
        if (g_app.img.valid &&
            View_ToImage(&g_app.view, g_app.img.w, g_app.img.h, client, &image_point)) {
            const unsigned char *pixel = g_app.img.px +
                (size_t)image_point.y * (size_t)g_app.img.pitch +
                (size_t)image_point.x * 4;
            _snprintf(cursor, sizeof(cursor), "(%d,%d) RGB=(%u,%u,%u)",
                      (int)image_point.x, (int)image_point.y,
                      (unsigned)pixel[2], (unsigned)pixel[1], (unsigned)pixel[0]);
        }
    }
    _snprintf(mode, sizeof(mode), "ROI:%d %s%s %.0f%%",
              g_app.rois.count, ROI_ModeLabel(g_app.mode),
              g_app.multi ? " Multi" : "", g_app.view.zoom * 100.0);
    _snprintf(time, sizeof(time), "L%.0f A%.0f H%.0f P%.0f S%.0f D%.0f ms",
              g_load_ms, g_analyze_ms, g_hist_ms, g_app.paint_ms,
              g_show_ms, g_done_ms);
    message = g_nav_status;
    if (g_browsing) {
        if (copy_utf8_to_acp(browsing, sizeof(browsing), "瀏覽中…"))
            message = browsing;
        else {
            OutputDebugStringA("ROI Analyzer: could not convert the browsing status to ANSI.\n");
            message = "Browsing...";
        }
    }
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, SB_PART_POS, (LPARAM)cursor);
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, SB_PART_MSG, (LPARAM)message);
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, SB_PART_MODE, (LPARAM)mode);
    App_StatusSetIndex();
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, SB_PART_TIME, (LPARAM)time);
}

void App_StatusSetIndex(void)
{
    char dash[8] = "-";
    if (!g_app.hwnd_status)
        return;
    if (!copy_utf8_to_acp(dash, sizeof(dash), "—"))
        OutputDebugStringA("ROI Analyzer: could not convert the index dash to ANSI.\n");
    if (g_current_exact && g_app.files.count > 0 &&
        g_app.file_idx >= 0 && g_app.file_idx < g_app.files.count)
        _snprintf(g_status_index, sizeof(g_status_index), "%d/%d",
                  g_app.file_idx + 1, g_app.files.count);
    else
        _snprintf(g_status_index, sizeof(g_status_index), "%s/%d", dash,
                  g_app.files.count);
    g_status_index[sizeof(g_status_index) - 1] = '\0';
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA,
                 SB_PART_INDEX | SBT_OWNERDRAW,
                 (LPARAM)g_status_index);
}

void App_StatusLayout(void)
{
    RECT client;
    int width, left = 220, mode = 200, index = 90, time = 260;
    int minimum_left = 80, minimum_mode = 90, minimum_index = 45;
    int minimum_time = 120;
    int msg;
    int parts[SB_PART_COUNT];
    if (!g_app.hwnd_status)
        return;
    SendMessageA(g_app.hwnd_status, SB_SIMPLE, FALSE, 0);
    GetClientRect(g_app.hwnd_status, &client);
    width = client.right;
    if (width < left + mode + index + time) {
        int shortage = left + mode + index + time - width;
        int reduce = time - minimum_time;
        if (reduce > shortage) reduce = shortage;
        time -= reduce;
        shortage -= reduce;
        reduce = mode - minimum_mode;
        if (reduce > shortage) reduce = shortage;
        mode -= reduce;
        shortage -= reduce;
        reduce = index - minimum_index;
        if (reduce > shortage) reduce = shortage;
        index -= reduce;
        shortage -= reduce;
        reduce = left - minimum_left;
        if (reduce > shortage) reduce = shortage;
        left -= reduce;
    }
    if (width < left + mode + index + time) {
        left = width / 4;
        mode = width / 4;
        index = width / 5;
        time = width - left - mode - index;
    }
    msg = width - left - mode - index - time;
    if (msg < 0)
        msg = 0;
    parts[0] = left;
    parts[1] = left + msg;
    parts[2] = parts[1] + mode;
    parts[3] = parts[2] + index;
    parts[4] = -1;
    SendMessageA(g_app.hwnd_status, SB_SETPARTS, SB_PART_COUNT, (LPARAM)parts);
}

void App_FlushPending(void)
{
    LARGE_INTEGER started;
    if (!g_app.analysis_stale) {
        g_browsing = FALSE;
        return;
    }
    QueryPerformanceCounter(&started);
    ROI_ReanalyzeAll(&g_app.rois, &g_app.img);
    g_analyze_ms = App_Ms(started);
    g_app.analysis_stale = FALSE;
    g_browsing = FALSE;
    if (strstr(g_nav_status, "ROI rebuilt"))
        strcpy(g_nav_status, "Analysis complete; ROI rebuilt");
    else if (strstr(g_nav_status, "ROI reused"))
        strcpy(g_nav_status, "Analysis complete; ROI reused");
    else
        strcpy(g_nav_status, "Analysis complete");
    App_RoiChanged();
    g_done_ms = App_Ms(g_nav_started);
    App_UpdateStatus();
}

static void App_UpdateTable(void)
{
    if (!g_app.hwnd_table)
        return;
    g_syncing_table = TRUE;
    if (!Table_RebuildState(g_app.hwnd_table, &g_app.rois,
                            ROI_ModeSource(g_app.table_page),
                            g_app.analysis_stale))
        MessageBoxA(g_app.hwnd_main, "Could not update the ROI table.",
                    "ROI Analyzer", MB_OK | MB_ICONERROR);
    g_syncing_table = FALSE;
}

BOOL App_NavKeyAllowed(const MSG *msg)
{
    char class_name[64];
    if (!msg || !g_app.img.valid || g_app.drag.dragging ||
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0 ||
        GetAncestor(msg->hwnd, GA_ROOT) != g_app.hwnd_main)
        return FALSE;
    if (!GetClassNameA(msg->hwnd, class_name, (int)sizeof(class_name)))
        return FALSE;
    if (_stricmp(class_name, WC_COMBOBOXA) == 0 ||
        _stricmp(class_name, WC_TABCONTROLA) == 0 ||
        _stricmp(class_name, WC_EDITA) == 0)
        return FALSE;
    return TRUE;
}

static BOOL rebuild_navigation_grids(BOOL had_grid3, BOOL had_grid5, BOOL light)
{
    BOOL too_small = FALSE;
    if (had_grid3) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID3);
        if (g_app.img.w < 3 || g_app.img.h < 3)
            too_small = TRUE;
        else if (!ROI_BuildGridPending(&g_app.rois, &g_app.img, 3))
            MessageBoxA(g_app.hwnd_main, "Could not rebuild the 3x3 ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
    }
    if (had_grid5) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID5);
        if (g_app.img.w < 5 || g_app.img.h < 5)
            too_small = TRUE;
        else if (!ROI_BuildGridPending(&g_app.rois, &g_app.img, 5))
            MessageBoxA(g_app.hwnd_main, "Could not rebuild the 5x5 ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
    }
    if (too_small && !light)
        MessageBoxA(g_app.hwnd_main,
                    "The image is too small for one or more existing grids; those grids were cleared.",
                    "ROI Analyzer", MB_OK | MB_ICONINFORMATION);
    return too_small;
}

void App_Navigate(int direction, BOOL light)
{
    image_t loaded, previous;
    LARGE_INTEGER load_started, analyze_started, show_started;
    BOOL exact = FALSE, same_size, had_grid3, had_grid5, grids_cleared = FALSE;
    int current, target, skipped = 0, skipped_non_acp = 0;
    char path[MAX_PATH];

    if (direction != -1 && direction != 1)
        return;
    if (!g_app.img.valid)
        return;
    g_browsing = FALSE;
    if (!light)
        App_FlushPending();
    if (g_app.drag.dragging) {
        g_app.drag.dragging = FALSE;
        ReleaseCapture();
    }
    Canvas_NavigationStarted();
    QueryPerformanceCounter(&load_started);
    g_nav_started = load_started;
    g_analyze_ms = 0.0;
    g_show_ms = 0.0;
    g_done_ms = 0.0;
    g_app.paint_ms = 0.0;
    if (!FileList_Refresh(&g_app.files, g_app.img.path)) {
        strcpy(g_nav_status, "Could not scan image folder");
        g_load_ms = App_Ms(load_started);
        App_UpdateStatus();
        return;
    }
    current = FileList_Find(&g_app.files, image_basename(g_app.img.path), &exact);
    if (current < 0) {
        strcpy(g_nav_status, "Could not refresh image list");
        g_load_ms = App_Ms(load_started);
        App_UpdateStatus();
        return;
    }
    g_app.file_idx = current;
    g_current_exact = exact;
    target = exact ? current + direction :
             (direction > 0 ? current : current - 1);
    if (target < 0 || target >= g_app.files.count) {
        MessageBeep(MB_ICONWARNING);
        strcpy(g_nav_status, direction > 0 ? "No next image" : "No previous image");
        g_load_ms = App_Ms(load_started);
        UpdateTitle();
        App_UpdateStatus();
        return;
    }
    ZeroMemory(&loaded, sizeof(loaded));
    while (target >= 0 && target < g_app.files.count && skipped < 10) {
        if (!FileList_Path(&g_app.files, target, path)) {
            skipped_non_acp++;
            target += direction;
            continue;
        }
        if (g_app.files.cloud[target]) {
            if (!copy_utf8_to_acp(g_nav_status, sizeof(g_nav_status),
                                  "雲端檔案下載中…")) {
                OutputDebugStringA("ROI Analyzer: could not convert the cloud status to ANSI.\n");
                strcpy(g_nav_status, "Cloud file downloading...");
            }
            App_UpdateStatus();
            UpdateWindow(g_app.hwnd_status);
        }
        if (Image_Load(&loaded, path) == 0)
            break;
        skipped++;
        target += direction;
    }
    g_load_ms = App_Ms(load_started);
    if (!loaded.valid) {
        MessageBeep(MB_ICONWARNING);
        if (skipped)
            _snprintf(g_nav_status, sizeof(g_nav_status),
                      "No loadable image (%d decode failures)", skipped);
        else
            strcpy(g_nav_status, "No representable image in that direction");
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
        UpdateTitle();
        App_UpdateStatus();
        return;
    }

    previous = g_app.img;
    same_size = previous.w == loaded.w && previous.h == loaded.h;
    had_grid3 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID3) > 0;
    had_grid5 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID5) > 0;
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    g_app.img = loaded;
    Image_Free(&previous);
    g_app.img_gen++;
    g_app.file_idx = target;
    g_current_exact = TRUE;
    g_app.analysis_stale = TRUE;
    g_browsing = light;
    if (!same_size) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_MANUAL);
        grids_cleared = rebuild_navigation_grids(had_grid3, had_grid5, light);
        ROI_SetSelected(&g_app.rois, -1);
        if (g_app.hwnd_canvas) {
            RECT rc;
            GetClientRect(g_app.hwnd_canvas, &rc);
            View_Reset(&g_app.view, &g_app.img, rc.right, rc.bottom);
        } else {
            View_Reset(&g_app.view, &g_app.img, 0, 0);
        }
    }
    if (skipped || skipped_non_acp) {
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "Loaded (%s); %s with skips (decode:%d non-ACP:%d)",
                  g_app.img.decoder, same_size ? "ROI reused" : "ROI rebuilt",
                  skipped, skipped_non_acp);
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
    } else {
        _snprintf(g_nav_status, sizeof(g_nav_status), "Loaded (%s); %s%s",
                  g_app.img.decoder, same_size ? "ROI reused" : "ROI rebuilt",
                  grids_cleared ? "; small-image grids cleared" : "");
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
    }
    UpdateTitle();
    App_StatusSetIndex();
    App_UpdateStatus();
    g_app.paint_pending = TRUE;
    QueryPerformanceCounter(&show_started);
    if (g_app.hwnd_canvas) {
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
        UpdateWindow(g_app.hwnd_canvas);
    }
    g_show_ms = App_Ms(show_started);
    if (light)
        Canvas_BuildPyramidNow();

    if (!light) {
        QueryPerformanceCounter(&analyze_started);
        ROI_ReanalyzeAll(&g_app.rois, &g_app.img);
        g_analyze_ms = App_Ms(analyze_started);
        g_app.analysis_stale = FALSE;
        g_browsing = FALSE;
    }
    App_UpdateTable();
    App_UpdateHistogram();
    if (!light)
        g_done_ms = App_Ms(g_nav_started);
    App_UpdateStatus();
}

void App_RoiChanged(void)
{
    App_UpdateTable();
    if (g_app.hwnd_canvas)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
    App_UpdateHistogram();
    App_UpdateStatus();
}

void App_UpdateHistogram(void)
{
    wchar_t label[160];
    LARGE_INTEGER started;
    QueryPerformanceCounter(&started);
    if (!g_app.show_hist || !g_app.hwnd_hist) {
        g_hist_ms = App_Ms(started);
        return;
    }
    if (!g_app.img.valid) {
        HistPanel_ClearSource(g_app.hwnd_hist);
        g_hist_ms = App_Ms(started);
        return;
    }
    if (g_app.rois.selected >= 0 && g_app.rois.selected < g_app.rois.count) {
        const roi_item_t *item = &g_app.rois.items[g_app.rois.selected];
        wchar_t mode_label[32];
        const char *mode_ascii;
        int number = ROI_SourceIndex(&g_app.rois, g_app.rois.selected) + 1;
        if (item->source == ROI_SRC_MANUAL)
            mode_ascii = ROI_ModeLabel(MODE_DRAG);
        else if (item->source == ROI_SRC_GRID3)
            mode_ascii = ROI_ModeLabel(MODE_GRID3);
        else
            mode_ascii = ROI_ModeLabel(MODE_GRID5);
        if (MultiByteToWideChar(CP_ACP, 0, mode_ascii, -1, mode_label,
                                (int)(sizeof(mode_label) / sizeof(mode_label[0]))) <= 0)
            mode_label[0] = L'\0';
        swprintf(label, sizeof(label) / sizeof(label[0]),
                 L"%ls #%d (%d,%d)-(%d,%d)", mode_label, number,
                 item->rc.left, item->rc.top, item->rc.right, item->rc.bottom);
        if (g_app.analysis_stale) {
            wcsncat(label, L" (pending)",
                    sizeof(label) / sizeof(label[0]) -
                    wcslen(label) - 1);
            HistPanel_SetLabel(g_app.hwnd_hist, label);
        } else {
            HistPanel_SetSource(g_app.hwnd_hist, &g_app.img, &item->rc, label,
                                g_app.img_gen);
        }
    } else {
        swprintf(label, sizeof(label) / sizeof(label[0]),
                 L"Entire Image (%dx%d)", g_app.img.w, g_app.img.h);
        if (g_app.analysis_stale) {
            wcsncat(label, L" (pending)",
                    sizeof(label) / sizeof(label[0]) -
                    wcslen(label) - 1);
            HistPanel_SetLabel(g_app.hwnd_hist, label);
        } else {
            HistPanel_SetSource(g_app.hwnd_hist, &g_app.img, NULL, label,
                                g_app.img_gen);
        }
    }
    g_hist_ms = App_Ms(started);
}

void App_PreviewHistogram(RECT img_rc)
{
    wchar_t label[160];

    if (!g_app.show_hist || !g_app.hwnd_hist || !g_app.img.valid)
        return;
    swprintf(label, sizeof(label) / sizeof(label[0]),
             L"Drag (preview) (%d,%d)-(%d,%d)",
             img_rc.left, img_rc.top, img_rc.right, img_rc.bottom);
    HistPanel_SetSource(g_app.hwnd_hist, &g_app.img, &img_rc, label,
                        g_app.img_gen);
}

void App_SetTablePage(roi_mode_t page)
{
    int tab = page == MODE_GRID3 ? 1 : (page == MODE_GRID5 ? 2 : 0);
    g_app.table_page = page;
    if (g_app.hwnd_tabs)
        TabCtrl_SetCurSel(g_app.hwnd_tabs, tab);
    App_SelectROI(-1);
}

void App_SelectROI(int global_index)
{
    roi_source_t source;
    if (global_index >= 0 && global_index < g_app.rois.count) {
        source = g_app.rois.items[global_index].source;
        ROI_SetSelected(&g_app.rois, global_index);
        if (source == ROI_SRC_GRID3)
            g_app.table_page = MODE_GRID3;
        else if (source == ROI_SRC_GRID5)
            g_app.table_page = MODE_GRID5;
        else
            g_app.table_page = MODE_DRAG;
        if (g_app.hwnd_tabs)
            TabCtrl_SetCurSel(g_app.hwnd_tabs,
                g_app.table_page == MODE_GRID3 ? 1 :
                (g_app.table_page == MODE_GRID5 ? 2 : 0));
    } else {
        ROI_SetSelected(&g_app.rois, -1);
    }
    App_RoiChanged();
    if (g_app.hwnd_table && g_app.rois.selected >= 0) {
        BOOL was_syncing = g_syncing_table;
        g_syncing_table = TRUE;
        Table_Select(g_app.hwnd_table, Table_FindRow(g_app.hwnd_table,
                                                     g_app.rois.selected));
        g_syncing_table = was_syncing;
    }
}

static void SetMode(roi_mode_t mode)
{
    int n = mode == MODE_GRID3 ? 3 : (mode == MODE_GRID5 ? 5 : 0);
    roi_source_t source = ROI_ModeSource(mode);

    if (g_app.drag.dragging) {
        g_app.drag.dragging = FALSE;
        ReleaseCapture();
    }
    g_app.mode = mode;
    if (n && g_app.img.valid && ROI_SourceCount(&g_app.rois, source) == 0) {
        if (g_app.img.w < n || g_app.img.h < n) {
            MessageBoxA(g_app.hwnd_main,
                        "The image must be at least as wide and high as the grid size.",
                        "ROI Analyzer", MB_OK | MB_ICONINFORMATION);
        } else if (!ROI_BuildGrid(&g_app.rois, &g_app.img, n)) {
            MessageBoxA(g_app.hwnd_main, "Could not create the ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
        }
    }
    CheckMenuItem(g_menu_mode, IDM_DRAG,
                  MF_BYCOMMAND | (mode == MODE_DRAG ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu_mode, IDM_GRID3,
                  MF_BYCOMMAND | (mode == MODE_GRID3 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu_mode, IDM_GRID5,
                  MF_BYCOMMAND | (mode == MODE_GRID5 ? MF_CHECKED : MF_UNCHECKED));
    UpdateTitle();
    App_SetTablePage(mode);
}

static void SetMulti(BOOL multi)
{
    g_app.multi = multi;
    if (g_app.hwnd_chk_multi)
        SendMessage(g_app.hwnd_chk_multi, BM_SETCHECK,
                    multi ? BST_CHECKED : BST_UNCHECKED, 0);
    CheckMenuItem(g_menu_mode, IDM_MULTI,
                  MF_BYCOMMAND | (multi ? MF_CHECKED : MF_UNCHECKED));
    UpdateTitle();
    App_UpdateStatus();
}

static void SetHistogramChannel(hist_channel_t channel)
{
    if (channel < HCH_RGB || channel >= HCH_COUNT)
        return;
    g_hist_channel = channel;
    HistPanel_SetChannel(g_app.hwnd_hist, channel);
    if (g_menu_view)
        CheckMenuRadioItem(g_menu_view, IDM_HIST_RGB, IDM_HIST_B,
                           channel == HCH_RGB ? IDM_HIST_RGB :
                           (channel == HCH_Y ? IDM_HIST_Y :
                           (channel == HCH_R ? IDM_HIST_R :
                           (channel == HCH_G ? IDM_HIST_G : IDM_HIST_B))),
                           MF_BYCOMMAND);
}

static void Layout(void)
{
    RECT client, status_rect;
    int width, height, status_height = 0;
    int table_height, button_height = 28, tabs_height = 28, canvas_height;
    int available, canvas_width, hist_width = 0;
    BOOL show_panel;

    if (!g_app.hwnd_main || !g_app.hwnd_status || !g_app.hwnd_canvas ||
        !g_app.hwnd_table || !g_app.hwnd_tabs || !g_app.hwnd_hist)
        return;
    SendMessage(g_app.hwnd_status, WM_SIZE, 0, 0);
    App_StatusLayout();
    GetClientRect(g_app.hwnd_main, &client);
    GetWindowRect(g_app.hwnd_status, &status_rect);
    status_height = status_rect.bottom - status_rect.top;
    width = client.right;
    show_panel = g_app.show_hist && width >= 520;
    if (show_panel) {
        hist_width = HISTPANEL_DEF_WIDTH;
        if (width - hist_width < 320) {
            hist_width = width - 320;
            if (hist_width < HISTPANEL_MIN_WIDTH)
                hist_width = HISTPANEL_MIN_WIDTH;
        }
        if (width - hist_width < 200) {
            show_panel = FALSE;
            hist_width = 0;
        }
    }
    canvas_width = width - hist_width;
    ShowWindow(g_app.hwnd_hist, show_panel ? SW_SHOWNA : SW_HIDE);
    if (show_panel)
        App_UpdateHistogram();
    height = client.bottom - status_height;
    if (height < 0)
        height = 0;
    available = height - button_height;
    table_height = (int)((double)client.bottom * 0.30);
    if (table_height < 140)
        table_height = 140;
    if (available - table_height < 100)
        table_height = available - 100;
    if (table_height < 0)
        table_height = 0;
    if (table_height < tabs_height)
        table_height = tabs_height;
    canvas_height = available - table_height;
    if (canvas_height < 0)
        canvas_height = 0;

    SetWindowPos(g_app.hwnd_canvas, NULL, 0, 0, canvas_width, canvas_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (show_panel)
        SetWindowPos(g_app.hwnd_hist, NULL, canvas_width, 0, hist_width,
                     canvas_height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_app.hwnd_btn_export, NULL, 8, canvas_height + 2, 78, 24,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_app.hwnd_btn_clear, NULL, 92, canvas_height + 2, 70, 24,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_app.hwnd_chk_multi, NULL, 172, canvas_height + 2, 100, 24,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_app.hwnd_tabs, NULL, 0, canvas_height + button_height,
                 width, tabs_height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_app.hwnd_table, NULL, 0, canvas_height + button_height + tabs_height,
                 width, table_height - tabs_height, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void OpenImageFile(const char *path)
{
    image_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (!path || !path[0])
        return;
    App_FlushPending();
    if (Image_Load(&loaded, path) != 0) {
        MessageBoxA(g_app.hwnd_main, "Cannot load image (PNG/JPG/BMP only).",
                    "Open", MB_OK | MB_ICONWARNING);
        return;
    }
    Image_Free(&g_app.img);
    g_app.img = loaded;
    ROI_Clear(&g_app.rois, &g_app.drag);
    g_app.img_gen++;
    g_app.analysis_stale = FALSE;
    g_app.file_idx = -1;
    g_current_exact = FALSE;
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    if (FileList_Refresh(&g_app.files, g_app.img.path)) {
        g_app.file_idx = FileList_Find(&g_app.files,
                                       image_basename(g_app.img.path),
                                       &g_current_exact);
        if (g_app.file_idx < 0) {
            _snprintf(g_nav_status, sizeof(g_nav_status),
                      "Loaded (%s); folder list refresh failed",
                      g_app.img.decoder);
            FileList_Free(&g_app.files);
        }
    } else {
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "Loaded (%s); folder scan failed", g_app.img.decoder);
        FileList_Free(&g_app.files);
    }
    if (g_current_exact)
        _snprintf(g_nav_status, sizeof(g_nav_status), "Loaded (%s)",
                  g_app.img.decoder);
    else if (g_app.files.scanned)
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "Loaded (%s); not in folder list", g_app.img.decoder);
    if (g_app.hwnd_canvas) {
        RECT canvas_rect;
        GetClientRect(g_app.hwnd_canvas, &canvas_rect);
        View_Reset(&g_app.view, &g_app.img,
                   canvas_rect.right, canvas_rect.bottom);
    } else {
        View_Reset(&g_app.view, &g_app.img, 0, 0);
    }
    if (g_app.mode == MODE_GRID3 || g_app.mode == MODE_GRID5) {
        int n = g_app.mode == MODE_GRID3 ? 3 : 5;
        if (g_app.img.w < n || g_app.img.h < n) {
            MessageBoxA(g_app.hwnd_main,
                        "The image is too small for the selected grid.",
                        "ROI Analyzer", MB_OK | MB_ICONINFORMATION);
        } else if (!ROI_BuildGrid(&g_app.rois, &g_app.img, n)) {
            MessageBoxA(g_app.hwnd_main, "Could not create the ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
        }
    }
    Layout();
    UpdateTitle();
    App_StatusSetIndex();
    App_RoiChanged();
}

static void OpenImageDialog(void)
{
    char file[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd_main;
    ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameA(&ofn))
        OpenImageFile(file);
}

static void ExportCurrent(void)
{
    char paths[3][MAX_PATH];
    char status[512] = "Exported:";
    int i, used = (int)strlen(status);
    static const roi_mode_t modes[] = { MODE_DRAG, MODE_GRID3, MODE_GRID5 };
    if (g_app.rois.count == 0) {
        MessageBoxA(g_app.hwnd_main, "尚無 ROI", "Export",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!g_app.img.valid || Export_Log(&g_app.img, &g_app.rois) != 0) {
        MessageBoxA(g_app.hwnd_main, "Could not open or write the export log.",
                    "Export", MB_OK | MB_ICONERROR);
        return;
    }
    for (i = 0; i < 3; i++) {
        roi_source_t source = i == 0 ? ROI_SRC_MANUAL :
                              (i == 1 ? ROI_SRC_GRID3 : ROI_SRC_GRID5);
        int added;
        if (ROI_SourceCount(&g_app.rois, source) == 0 ||
            Export_GetPath(&g_app.img, modes[i], paths[i], sizeof(paths[i])) != 0)
            continue;
        if ((size_t)used < sizeof(status)) {
            added = _snprintf(status + used, sizeof(status) - (size_t)used,
                              "%s%s", used > 10 ? ", " : " ", paths[i]);
            if (added >= (int)(sizeof(status) - (size_t)used)) {
                used = (int)sizeof(status) - 1;
                break;
            }
            if (added > 0)
                used += added;
        }
    }
    status[sizeof(status) - 1] = '\0';
    strncpy(g_nav_status, status, sizeof(g_nav_status) - 1);
    g_nav_status[sizeof(g_nav_status) - 1] = '\0';
    App_UpdateStatus();
}

static void OpenLogFile(void)
{
    char path[MAX_PATH];
    if (!g_app.img.valid) {
        MessageBoxA(g_app.hwnd_main, "No current image.", "Log",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (Export_GetPath(&g_app.img, g_app.mode, path, sizeof(path)) == 0 &&
        GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        if ((INT_PTR)ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) <= 32)
            MessageBoxA(g_app.hwnd_main, "Could not open the log file.", "Log",
                        MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxA(g_app.hwnd_main, "The current image has no log file.",
                "Log", MB_OK | MB_ICONINFORMATION);
}

static void OpenImageFolder(void)
{
    char folder[MAX_PATH];
    char *slash;
    if (!g_app.img.valid) {
        MessageBoxA(g_app.hwnd_main, "No current image.", "Log",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    strncpy(folder, g_app.img.path, sizeof(folder) - 1);
    folder[sizeof(folder) - 1] = '\0';
    slash = strrchr(folder, '\\');
    if (!slash)
        slash = strrchr(folder, '/');
    if (slash)
        *slash = '\0';
    else
        strcpy(folder, ".");
    if ((INT_PTR)ShellExecuteA(NULL, "open", folder, NULL, NULL, SW_SHOWNORMAL) <= 32)
        MessageBoxA(g_app.hwnd_main, "Could not open the image folder.", "Log",
                    MB_OK | MB_ICONERROR);
}

static void ClearAll(void)
{
    ROI_Clear(&g_app.rois, &g_app.drag);
    App_RoiChanged();
}

static void ClearCurrent(void)
{
    ROI_ClearSource(&g_app.rois, ROI_ModeSource(g_app.table_page));
    App_RoiChanged();
}

static void ChangeZoom(float factor)
{
    RECT rc;
    POINT center;
    if (!g_app.hwnd_canvas || !g_app.img.valid)
        return;
    GetClientRect(g_app.hwnd_canvas, &rc);
    center.x = rc.right / 2;
    center.y = rc.bottom / 2;
    View_SetZoom(&g_app.view, &g_app.img, rc.right, rc.bottom,
                 g_app.view.zoom * factor, center);
    InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
    App_UpdateStatus();
}

static void DeleteSelected(void)
{
    if (g_app.mode == MODE_DRAG && g_app.rois.selected >= 0) {
        ROI_Remove(&g_app.rois, g_app.rois.selected);
        App_SelectROI(-1);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wparam,
                                    LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        HINSTANCE instance = ((CREATESTRUCTA *)lparam)->hInstance;
        g_main_wm_create_started = TRUE;
        g_app.hwnd_main = hwnd;
        g_app.hwnd_status = CreateWindowExA(0, STATUSCLASSNAMEA, "",
                                             WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                             0, 0, 0, 0, hwnd, (HMENU)2000,
                                             instance, NULL);
        if (!g_app.hwnd_status) {
            ReportCreateWindowFailureA("StatusBar");
            return -1;
        }
        g_app.hwnd_canvas = CreateWindowExA(0, "RoiAnalyzerCanvas", "",
                                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                             0, 0, 0, 0, hwnd, (HMENU)IDC_CANVAS,
                                             instance, NULL);
        if (!g_app.hwnd_canvas) {
            ReportCreateWindowFailureA("Canvas");
            return -1;
        }
        g_app.hwnd_btn_export = CreateWindowExA(0, "BUTTON", "Export",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  0, 0, 0, 0, hwnd,
                                                  (HMENU)IDC_EXPORT, instance, NULL);
        if (!g_app.hwnd_btn_export) {
            ReportCreateWindowFailureA("Export button");
            return -1;
        }
        g_app.hwnd_btn_clear = CreateWindowExA(0, "BUTTON", "Clear",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                0, 0, 0, 0, hwnd,
                                                (HMENU)IDC_CLEAR, instance, NULL);
        if (!g_app.hwnd_btn_clear) {
            ReportCreateWindowFailureA("Clear button");
            return -1;
        }
        g_app.hwnd_chk_multi = CreateWindowExA(0, "BUTTON", "Multi",
                                                WS_CHILD | WS_VISIBLE |
                                                BS_AUTOCHECKBOX,
                                                0, 0, 0, 0, hwnd,
                                                (HMENU)IDC_MULTI, instance, NULL);
        if (!g_app.hwnd_chk_multi) {
            ReportCreateWindowFailureA("Multi checkbox");
            return -1;
        }
        g_app.hwnd_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
                                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                           TCS_TABS,
                                           0, 0, 0, 0, hwnd,
                                           (HMENU)IDC_TABS, instance, NULL);
        if (!g_app.hwnd_tabs) {
            ReportCreateWindowFailureA("Tab");
            return -1;
        }
        g_app.hwnd_hist = HistPanel_Create(hwnd, IDC_HISTPANEL);
        if (!g_app.hwnd_hist) {
            ReportCreateWindowFailureA("Histogram panel");
            return -1;
        }
        g_app.hwnd_table = Table_Create(hwnd, instance, IDC_TABLE);
        if (!g_app.hwnd_table) {
            ReportCreateWindowFailureA("ROI table");
            return -1;
        }
        if (g_app.hwnd_tabs) {
            TCITEMA tab;
            static const char *const labels[] = { "Drag", "3x3", "5x5" };
            int i;
            ZeroMemory(&tab, sizeof(tab));
            tab.mask = TCIF_TEXT;
            for (i = 0; i < 3; i++) {
                tab.pszText = (LPSTR)labels[i];
                TabCtrl_InsertItem(g_app.hwnd_tabs, i, &tab);
            }
        }
        DragAcceptFiles(hwnd, TRUE);
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        App_UpdateStatus();
        return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
        if (draw && draw->hwndItem == g_app.hwnd_status &&
            draw->itemID == SB_PART_INDEX) {
            const char *text = (const char *)draw->itemData;
            FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DFACE));
            SetBkMode(draw->hDC, TRANSPARENT);
            DrawTextA(draw->hDC, text ? text : "", -1, &draw->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
            App_FlushPending();
        break;
    case WM_COMMAND: {
        int id = LOWORD(wparam);
        App_FlushPending();
        if (id == IDM_OPEN)
            OpenImageDialog();
        else if (id == IDM_EXPORT || id == IDC_EXPORT)
            ExportCurrent();
        else if (id == IDM_EXIT)
            DestroyWindow(hwnd);
        else if (id == IDM_PREVIOUS)
            App_Navigate(-1, FALSE);
        else if (id == IDM_NEXT)
            App_Navigate(1, FALSE);
        else if (id == IDM_DRAG)
            SetMode(MODE_DRAG);
        else if (id == IDM_GRID3)
            SetMode(MODE_GRID3);
        else if (id == IDM_GRID5)
            SetMode(MODE_GRID5);
        else if (id == IDM_MULTI)
            SetMulti(!g_app.multi);
        else if (id == IDM_DELETE)
            DeleteSelected();
        else if (id == IDM_CLEAR || id == IDC_CLEAR)
            ClearCurrent();
        else if (id == IDM_CLEAR_ALL)
            ClearAll();
        else if (id == IDM_ZOOM_IN)
            ChangeZoom(1.1f);
        else if (id == IDM_ZOOM_OUT)
            ChangeZoom(1.0f / 1.1f);
        else if (id == IDM_OPENLOG)
            OpenLogFile();
        else if (id == IDM_FOLDER)
            OpenImageFolder();
        else if (id == IDM_HISTOGRAM) {
            g_app.show_hist = !g_app.show_hist;
            CheckMenuItem(g_menu_view, IDM_HISTOGRAM,
                          MF_BYCOMMAND | (g_app.show_hist ? MF_CHECKED : MF_UNCHECKED));
            Layout();
            App_UpdateHistogram();
        } else if (id == IDM_HIST_RGB)
            SetHistogramChannel(HCH_RGB);
        else if (id == IDM_HIST_Y)
            SetHistogramChannel(HCH_Y);
        else if (id == IDM_HIST_R)
            SetHistogramChannel(HCH_R);
        else if (id == IDM_HIST_G)
            SetHistogramChannel(HCH_G);
        else if (id == IDM_HIST_B)
            SetHistogramChannel(HCH_B);
        else if (id == IDM_HIST_LOG) {
            g_hist_log = !g_hist_log;
            CheckMenuItem(g_menu_view, IDM_HIST_LOG,
                          MF_BYCOMMAND | (g_hist_log ? MF_CHECKED : MF_UNCHECKED));
            HistPanel_SetLogScale(g_app.hwnd_hist, g_hist_log);
        }
        else if (id == IDC_MULTI && HIWORD(wparam) == BN_CLICKED)
            SetMulti(SendMessage(g_app.hwnd_chk_multi, BM_GETCHECK, 0, 0) ==
                     BST_CHECKED);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (header->hwndFrom == g_app.hwnd_tabs &&
            header->code == TCN_SELCHANGE) {
            int selected = TabCtrl_GetCurSel(g_app.hwnd_tabs);
            App_SetTablePage(selected == 1 ? MODE_GRID3 :
                             (selected == 2 ? MODE_GRID5 : MODE_DRAG));
        } else if (header->hwndFrom == g_app.hwnd_table &&
            header->code == LVN_ITEMCHANGED && !g_syncing_table) {
            NMLISTVIEW *change = (NMLISTVIEW *)lparam;
            if ((change->uNewState & LVIS_SELECTED) ||
                ((change->uOldState & LVIS_SELECTED) &&
                 !(change->uNewState & LVIS_SELECTED)))
                App_SelectROI(Table_SelectedGlobalIndex(g_app.hwnd_table));
        } else if (header->hwndFrom == g_app.hwnd_hist &&
                   header->code == HPN_CHANNELCHANGED) {
            nm_histpanel_t *change = (nm_histpanel_t *)lparam;
            SetHistogramChannel(change->channel);
        } else if (header->hwndFrom == g_app.hwnd_hist &&
                   header->code == HPN_LOGSCALECHANGED) {
            nm_histpanel_t *change = (nm_histpanel_t *)lparam;
            g_hist_log = change->log_scale;
            CheckMenuItem(g_menu_view, IDM_HIST_LOG,
                          MF_BYCOMMAND | (g_hist_log ? MF_CHECKED : MF_UNCHECKED));
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wparam;
        char path[MAX_PATH] = { 0 };
        if (DragQueryFileA(drop, 0, path, MAX_PATH))
            OpenImageFile(path);
        DragFinish(drop);
        return 0;
    }
    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static HMENU CreateMainMenu(void)
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU mode = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU log = CreatePopupMenu();
    HMENU view = CreatePopupMenu();

    AppendMenuA(file, MF_STRING, IDM_OPEN, "Open...\tO");
    AppendMenuA(file, MF_STRING, IDM_PREVIOUS, "Previous Image");
    AppendMenuA(file, MF_STRING, IDM_NEXT, "Next Image");
    AppendMenuA(file, MF_STRING, IDM_EXPORT, "Export Log\tCtrl+E");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_EXIT, "Exit");
    AppendMenuA(mode, MF_STRING | MF_CHECKED, IDM_DRAG, "Drag\t1");
    AppendMenuA(mode, MF_STRING, IDM_GRID3, "3x3 Grid\t2");
    AppendMenuA(mode, MF_STRING, IDM_GRID5, "5x5 Grid\t3");
    AppendMenuA(mode, MF_SEPARATOR, 0, NULL);
    AppendMenuA(mode, MF_STRING, IDM_MULTI, "Multi Select\tM");
    AppendMenuA(edit, MF_STRING, IDM_DELETE, "Delete Selected ROI\tDel");
    AppendMenuA(edit, MF_STRING, IDM_CLEAR, "Clear Current Tab ROI\tC");
    AppendMenuA(edit, MF_STRING, IDM_CLEAR_ALL, "Clear All ROI\tShift+C");
    AppendMenuA(log, MF_STRING, IDM_OPENLOG, "Open Log File");
    AppendMenuA(log, MF_STRING, IDM_FOLDER, "Open Folder");
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_HISTOGRAM, "Histogram Panel\tH");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_HIST_RGB, "Channel: RGB\tA");
    AppendMenuA(view, MF_STRING, IDM_HIST_Y, "Luminosity (Y)\tY");
    AppendMenuA(view, MF_STRING, IDM_HIST_R, "Red\tR");
    AppendMenuA(view, MF_STRING, IDM_HIST_G, "Green\tG");
    AppendMenuA(view, MF_STRING, IDM_HIST_B, "Blue\tB");
    AppendMenuA(view, MF_STRING, IDM_HIST_LOG, "Log Scale\tL");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)mode, "Mode");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "View");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)log, "Log");
    g_menu_mode = mode;
    g_menu_view = view;
    return bar;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show)
{
    WNDCLASSA wc;
    HMENU menu;
    ACCEL accelerators[] = {
        { FVIRTKEY, 'H', IDM_HISTOGRAM },
        { FVIRTKEY, 'A', IDM_HIST_RGB },
        { FVIRTKEY, 'Y', IDM_HIST_Y },
        { FVIRTKEY, 'R', IDM_HIST_R },
        { FVIRTKEY, 'G', IDM_HIST_G },
        { FVIRTKEY, 'B', IDM_HIST_B },
        { FVIRTKEY, 'L', IDM_HIST_LOG }
    };
    MSG msg;
    int screen_width, screen_height, width, height, x, y;
    int result;
    DWORD s_nav_done_tick;
    HRESULT com_result;

    (void)previous;
    memset(&g_app, 0, sizeof(g_app));
    g_app.file_idx = -1;
    com_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) {
        MessageBoxA(NULL, "COM initialization failed.", "ROI Analyzer",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    g_app.mode = MODE_DRAG;
    g_app.table_page = MODE_DRAG;
    g_app.show_hist = TRUE;
    g_app.view.zoom = 1.0f;
    QueryPerformanceFrequency(&g_qpc_frequency);
    ROI_Init(&g_app.rois, &g_app.drag);
    App_InitCommonControls();
    {
        GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
        if (GdiplusStartup(&g_gdiplus, &input, NULL) != Ok) {
            MessageBoxA(NULL, "GDI+ initialization failed.", "ROI Analyzer",
                        MB_OK | MB_ICONERROR);
            CoUninitialize();
            return 1;
        }
    }
    if (!Canvas_Register(instance)) {
        DWORD error = GetLastError();
        char message[128];
        snprintf(message, sizeof(message),
                 "Could not register the Canvas class (GetLastError: %lu).",
                 (unsigned long)error);
        MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplus);
        CoUninitialize();
        return 1;
    }
    if (!HistPanel_Register(instance)) {
        DWORD error = GetLastError();
        char message[128];
        snprintf(message, sizeof(message),
                 "Could not register the Histogram class (GetLastError: %lu).",
                 (unsigned long)error);
        MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplus);
        CoUninitialize();
        return 1;
    }
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "RoiAnalyzerMain";
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Could not register the main window class.",
                    "ROI Analyzer", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplus);
        CoUninitialize();
        return 1;
    }
    menu = CreateMainMenu();
    g_accelerators = CreateAcceleratorTableA(accelerators,
                            (int)(sizeof(accelerators) / sizeof(accelerators[0])));
    screen_width = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);
    width = screen_width > 0 ? screen_width / 2 : 1280;
    height = screen_height > 0 ? screen_height / 2 : 800;
    x = screen_width > 0 ? screen_width / 4 : CW_USEDEFAULT;
    y = screen_height > 0 ? screen_height / 4 : CW_USEDEFAULT;
    g_app.hwnd_main = CreateWindowExA(WS_EX_ACCEPTFILES, "RoiAnalyzerMain",
                                      "ROI Analyzer",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                      x, y, width, height, NULL, menu, instance, NULL);
    if (!g_app.hwnd_main) {
        if (!g_main_wm_create_started)
            ReportCreateWindowFailureA("main window");
        ROI_Destroy(&g_app.rois);
        GdiplusShutdown(g_gdiplus);
        CoUninitialize();
        return 1;
    }
    ShowWindow(g_app.hwnd_main, show);
    UpdateWindow(g_app.hwnd_main);
    UpdateTitle();
    App_UpdateStatus();
    if (command_line && command_line[0]) {
        char path[MAX_PATH];
        size_t length = strlen(command_line);
        strncpy(path, command_line, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        if (length >= 2 && path[0] == '"' && path[length - 1] == '"') {
            path[length - 1] = '\0';
            memmove(path, path + 1, length - 1);
        }
        OpenImageFile(path);
    }
    s_nav_done_tick = GetTickCount();
    while ((result = GetMessageA(&msg, NULL, 0, 0)) > 0) {
        if (msg.message == WM_KEYUP &&
            (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT))
            App_FlushPending();
        if (msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) &&
            App_NavKeyAllowed(&msg)) {
            BOOL repeat = (msg.lParam & (1L << 30)) != 0;
            if (repeat && (LONG)(msg.time - s_nav_done_tick) < 0)
                continue;
            App_Navigate(msg.wParam == VK_LEFT ? -1 : 1, repeat);
            s_nav_done_tick = GetTickCount();
            continue;
        }
        if (!TranslateAcceleratorA(g_app.hwnd_main, g_accelerators, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    if (g_accelerators)
        DestroyAcceleratorTable(g_accelerators);
    Image_Free(&g_app.img);
    ViewPyr_Free(&g_app.pyramid);
    FileList_Free(&g_app.files);
    ROI_Destroy(&g_app.rois);
    GdiplusShutdown(g_gdiplus);
    CoUninitialize();
    return result == -1 ? 1 : (int)msg.wParam;
}
