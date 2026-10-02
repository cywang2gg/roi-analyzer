#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus/gdiplus.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <objbase.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "app.h"
#include "canvas.h"
#include "compare.h"
#include "app_messages.h"
#include "detect.h"
#include "export.h"
#include "histpanel.h"
#include "image_save.h"
#include "monitor.h"
#include "rename.h"
#include "rotate.h"
#include "settings.h"
#include "table.h"

#define IDM_OPEN       101
#define IDM_EXPORT     102
#define IDM_EXIT       103
#define IDM_PREVIOUS   104
#define IDM_NEXT       105
#define IDM_SAVE_IMAGE 106
#define IDM_RENAME_FILE 107
#define IDM_MONITOR_SETTINGS 108
#define IDM_COMPARE_METRICS CMP_ID_METRICS
#define IDM_METRICS_SETTINGS 110
#define IDM_DRAG       111
#define IDM_GRID3      112
#define IDM_GRID5      113
#define IDM_MULTI      114
#define IDM_ROT90      115
#define IDM_ROT180     116
#define IDM_ROT270     117
#define IDM_ROT_ANY    118
#define IDM_DELETE     121
#define IDM_CLEAR      122
#define IDM_CLEAR_ALL  123
#define IDM_OPENCSV    131
#define IDM_FOLDER     132
#define IDM_ZOOM_IN    141
#define IDM_ZOOM_OUT   142
#define IDM_HISTOGRAM  151
#define IDM_HIST_RGB   161
#define IDM_HIST_Y     162
#define IDM_HIST_R     163
#define IDM_HIST_G     164
#define IDM_HIST_B     165
#define IDM_HIST_LOG   166
#define IDC_CANVAS     1001
#define IDC_TABLE      1002
#define IDC_EXPORT     1003
#define IDC_CLEAR      1004
#define IDC_MULTI      1005
#define IDC_TABS       1006
#define IDC_HISTPANEL  1007
#define IDD_ROTATE_ANGLE 201
#define IDC_ROTATE_ANGLE 202
#define IDD_MONITOR_SETTINGS 210
#define IDC_MON_PATH1 211
#define IDC_MON_PATH2 212
#define IDC_MON_PATH3 213
#define IDC_MON_BROWSE1 214
#define IDC_MON_BROWSE2 215
#define IDC_MON_BROWSE3 216
#define IDC_MON_ACTIVE1 217
#define IDC_MON_ACTIVE2 218
#define IDC_MON_ACTIVE3 219
#define IDD_NEW_FILE_PROMPT 220
#define IDC_PROMPT_THUMB 221
#define IDC_PROMPT_NAME 222
#define IDC_PROMPT_BTN_RENAME 223
#define IDC_PROMPT_BTN_RENAME_OPEN 224
#define IDC_PROMPT_BTN_COMPARE_ADD 225
#define IDC_PROMPT_BTN_COMPARE_NOW 226
#define IDD_RENAME_INPUT 230
#define IDC_RENAME_EDIT 231
#define SB_PART_POS    0
#define SB_PART_MSG    1
#define SB_PART_MODE   2
#define SB_PART_INDEX  3
#define SB_PART_TIME   4
#define SB_PART_COUNT  5
#define WM_APP_DRAIN_NEW_FILES (WM_APP + 102)
#define PROMPT_QUEUE_CAPACITY 64
#define PROMPT_DRAIN_TIMER 1

app_t g_app;

static ULONG_PTR g_gdiplus;
static HACCEL g_accelerators;
static BOOL g_syncing_table;
static BOOL g_main_wm_create_started;
static HMENU g_menu_mode;
static HMENU g_menu_view;
static HMENU g_menu_image;
static BOOL g_discard_approved;
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
static char s_persistent_prefix[MAX_PATH];
static char *s_pending_files[PROMPT_QUEUE_CAPACITY];
static size_t s_pending_head;
static size_t s_pending_count;
static unsigned int s_dropped_files;
static BOOL s_prompt_open;

static BOOL App_InitCommonControls(void);
static void Layout(void);
static void SetMode(roi_mode_t mode);
static void SetMulti(BOOL multi);
static void ExportCurrent(void);
static void ChangeZoom(float factor);
static void SetHistogramChannel(hist_channel_t channel);
static void UpdateTitle(void);
static BOOL App_SaveImage(void);
static BOOL App_ConfirmDiscard(void);
static void App_RotateOrthogonal(int steps);
static void App_RotateArbitrary(double degrees);
static void App_UpdateImageMenu(void);
static void App_UpdateTable(void);
static void CompareFilesDialog(void);
static void CompareCurrentWithNext(void);
static BOOL App_CompareOpenPaths(char paths[CMP_MAX_CELLS][MAX_PATH],
                                 int count, cmp_open_mode_t mode);
static void App_OnDropFiles(HDROP drop);
static void App_HandleNewFileArrival(const char *path);
static void App_DrainPromptQueue(void);
static void App_SchedulePromptQueue(void);
static void App_ReplaceImage(image_t *loaded);
static void App_UnloadImage(void);

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
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES |
                ICC_PROGRESS_CLASS;
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
    const char *base = PathFindFileNameA(path);
    return base ? base : path;
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
                      "ROI Analyzer - %s %dx%d [%d/%d] [%s] [%s]%s",
                      image_basename(g_app.img.path), g_app.img.w, g_app.img.h,
                      g_app.file_idx + 1, g_app.files.count,
                      ROI_ModeLabel(g_app.mode), g_app.multi ? "Multi" : "Single",
                      g_app.is_modified ? " *" : "");
        else
            _snprintf(title, sizeof(title),
                      "ROI Analyzer - %s %dx%d [%s/%d] [%s] [%s]%s",
                      image_basename(g_app.img.path), g_app.img.w, g_app.img.h,
                      dash, g_app.files.count, ROI_ModeLabel(g_app.mode),
                      g_app.multi ? "Multi" : "Single",
                      g_app.is_modified ? " *" : "");
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
    char detect_status[256];
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
    _snprintf(mode, sizeof(mode), "ROI:%d %s%s %.0f%%%s",
              g_app.rois.count, ROI_ModeLabel(g_app.mode),
              g_app.multi ? " Multi" : "", g_app.view.zoom * 100.0,
              g_app.is_modified ? " *" : "");
    _snprintf(time, sizeof(time), "L%.0f A%.0f H%.0f P%.0f S%.0f D%.0f ms",
              g_load_ms, g_analyze_ms, g_hist_ms, g_app.paint_ms,
              g_show_ms, g_done_ms);
    message = g_nav_status;
    Detect_GetStatusText(detect_status, sizeof(detect_status));
    if (detect_status[0] != '\0')
        message = detect_status;
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

static void App_ReplaceImage(image_t *loaded)
{
    image_t previous = g_app.img;

    Detect_OnImageUnloading();
    g_app.img = *loaded;
    ZeroMemory(loaded, sizeof(*loaded));
    Image_Free(&previous);
    Detect_OnImageLoaded();
}

static void App_UnloadImage(void)
{
    Detect_OnImageUnloading();
    Image_Free(&g_app.img);
}

static BOOL App_SaveImage(void)
{
    if (!g_app.img.valid)
        return FALSE;
    if (Image_SavePNG(&g_app.img, g_app.img.path) != 0) {
        MessageBoxA(g_app.hwnd_main, "Could not save image (PNG encode failed).",
                    "Save Image", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    g_app.is_modified = FALSE;
    _snprintf(g_nav_status, sizeof(g_nav_status), "Saved %s",
              image_basename(g_app.img.path));
    g_nav_status[sizeof(g_nav_status) - 1] = '\0';
    UpdateTitle();
    App_UpdateStatus();
    return TRUE;
}

static BOOL App_ConfirmDiscard(void)
{
    int result;
    if (!g_app.is_modified || !g_app.img.valid || g_discard_approved)
        return TRUE;
    result = MessageBoxA(g_app.hwnd_main,
        "Image has been rotated but not saved. Save changes?",
        "ROI Analyzer", MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDYES)
        return App_SaveImage();
    if (result == IDNO) {
        g_discard_approved = TRUE;
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK RotateAngleDlgProc(HWND dialog, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrA(dialog, GWLP_USERDATA, (LONG_PTR)lparam);
        SetDlgItemTextA(dialog, IDC_ROTATE_ANGLE, "0");
        SetFocus(GetDlgItem(dialog, IDC_ROTATE_ANGLE));
        return FALSE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    if (LOWORD(wparam) == IDOK) {
        BOOL translated = FALSE;
        UINT value = GetDlgItemInt(dialog, IDC_ROTATE_ANGLE, &translated, TRUE);
        double *degrees = (double *)GetWindowLongPtrA(dialog, GWLP_USERDATA);
        if (!translated || (int)value < -360 || (int)value > 360) {
            MessageBoxA(dialog, "Enter an integer from -360 to 360.",
                        "Rotate Arbitrary", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        *degrees = (double)(int)value;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void App_UpdateImageMenu(void)
{
    static const UINT commands[] = {
        IDM_SAVE_IMAGE, IDM_RENAME_FILE, IDM_ROT90, IDM_ROT180, IDM_ROT270,
        IDM_ROT_ANY
    };
    HMENU menu = GetMenu(g_app.hwnd_main);
    UINT state = g_app.img.valid ? MF_ENABLED : MF_GRAYED;
    size_t i;
    if (!menu)
        return;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        EnableMenuItem(menu, commands[i], MF_BYCOMMAND | state);
    CheckMenuItem(g_menu_view, IDM_METRICS_SETTINGS, MF_BYCOMMAND |
                  (Compare_MetricsStage2Enabled() ?
                   MF_CHECKED : MF_UNCHECKED));
}

static void App_RotateOrthogonal(int steps)
{
    int old_w, old_h, i, degrees;
    BOOL rotated;
    BOOL small_grid = FALSE;

    if (!g_app.img.valid)
        return;
    Compare_CloseAll();
    old_w = g_app.img.w;
    old_h = g_app.img.h;
    degrees = steps == 1 ? 90 : (steps == 2 ? 180 : 270);
    if (steps == 1)
        rotated = Image_Rotate90(&g_app.img);
    else if (steps == 2)
        rotated = Image_Rotate180(&g_app.img);
    else
        rotated = Image_Rotate270(&g_app.img);
    if (!rotated) {
        MessageBoxA(g_app.hwnd_main, "Could not rotate image (out of memory).",
                    "Rotate Image", MB_OK | MB_ICONERROR);
        return;
    }
    for (i = 0; i < g_app.rois.count; i++) {
        RECT *rc = &g_app.rois.items[i].rc;
        if (degrees == 90)
            ROI_RotateRect90(rc, old_w, old_h);
        else if (degrees == 180)
            ROI_RotateRect180(rc, old_w, old_h);
        else
            ROI_RotateRect270(rc, old_w, old_h);
    }
    if ((g_app.img.w < 3 || g_app.img.h < 3) &&
        ROI_SourceCount(&g_app.rois, ROI_SRC_GRID3)) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID3);
        small_grid = TRUE;
    }
    if ((g_app.img.w < 5 || g_app.img.h < 5) &&
        ROI_SourceCount(&g_app.rois, ROI_SRC_GRID5)) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID5);
        small_grid = TRUE;
    }
    ROI_ReanalyzeAll(&g_app.rois, &g_app.img);
    if (g_app.drag.dragging) {
        g_app.drag.dragging = FALSE;
        ReleaseCapture();
    }
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    if (g_app.hwnd_canvas) {
        RECT canvas;
        GetClientRect(g_app.hwnd_canvas, &canvas);
        View_Reset(&g_app.view, &g_app.img, canvas.right, canvas.bottom);
    } else {
        View_Reset(&g_app.view, &g_app.img, 0, 0);
    }
    g_app.img_gen++;
    g_app.analysis_stale = TRUE;
    g_app.is_modified = TRUE;
    _snprintf(g_nav_status, sizeof(g_nav_status),
              degrees == 180 ? "Rotated 180 degrees; unsaved" :
              (degrees == 90 ? "Rotated 90 CW; unsaved" :
                               "Rotated 270 CW; unsaved"));
    UpdateTitle();
    App_RoiChanged();
    App_UpdateHistogram();
    if (small_grid)
        MessageBoxA(g_app.hwnd_main,
                    "The image is too small for one or more existing grids; those grids were cleared.",
                    "ROI Analyzer", MB_OK | MB_ICONINFORMATION);
}

static void App_RotateArbitrary(double degrees)
{
    BOOL had_grid3, had_grid5, small_grid = FALSE;
    if (!g_app.img.valid || !isfinite(degrees) ||
        fmod(degrees, 360.0) == 0.0)
        return;
    Compare_CloseAll();
    if (!Image_RotateArbitrary(&g_app.img, degrees)) {
        MessageBoxA(g_app.hwnd_main,
                    "Could not rotate image (invalid angle or out of memory).",
                    "Rotate Image", MB_OK | MB_ICONERROR);
        return;
    }
    had_grid3 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID3) > 0;
    had_grid5 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID5) > 0;
    ROI_ClearSource(&g_app.rois, ROI_SRC_MANUAL);
    if (had_grid3) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID3);
        if (g_app.img.w < 3 || g_app.img.h < 3)
            small_grid = TRUE;
        else if (!ROI_BuildGrid(&g_app.rois, &g_app.img, 3))
            MessageBoxA(g_app.hwnd_main, "Could not rebuild the 3x3 ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
    }
    if (had_grid5) {
        ROI_ClearSource(&g_app.rois, ROI_SRC_GRID5);
        if (g_app.img.w < 5 || g_app.img.h < 5)
            small_grid = TRUE;
        else if (!ROI_BuildGrid(&g_app.rois, &g_app.img, 5))
            MessageBoxA(g_app.hwnd_main, "Could not rebuild the 5x5 ROI grid.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
    }
    ROI_ReanalyzeAll(&g_app.rois, &g_app.img);
    if (g_app.drag.dragging) {
        g_app.drag.dragging = FALSE;
        ReleaseCapture();
    }
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    if (g_app.hwnd_canvas) {
        RECT canvas;
        GetClientRect(g_app.hwnd_canvas, &canvas);
        View_Reset(&g_app.view, &g_app.img, canvas.right, canvas.bottom);
    } else {
        View_Reset(&g_app.view, &g_app.img, 0, 0);
    }
    g_app.img_gen++;
    g_app.analysis_stale = TRUE;
    g_app.is_modified = TRUE;
    _snprintf(g_nav_status, sizeof(g_nav_status),
              "Rotated %.0f degrees; unsaved", degrees);
    UpdateTitle();
    App_RoiChanged();
    App_UpdateHistogram();
    if (small_grid)
        MessageBoxA(g_app.hwnd_main,
                    "The image is too small for one or more existing grids; those grids were cleared.",
                    "ROI Analyzer", MB_OK | MB_ICONINFORMATION);
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
    image_t loaded;
    LARGE_INTEGER load_started, analyze_started, show_started;
    BOOL exact = FALSE, same_size, had_grid3, had_grid5, grids_cleared = FALSE;
    int current, target, skipped = 0, skipped_non_acp = 0;
    char path[MAX_PATH];

    if (direction != -1 && direction != 1)
        return;
    if (!App_ConfirmDiscard())
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

    same_size = g_app.img.w == loaded.w && g_app.img.h == loaded.h;
    had_grid3 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID3) > 0;
    had_grid5 = ROI_SourceCount(&g_app.rois, ROI_SRC_GRID5) > 0;
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    App_ReplaceImage(&loaded);
    g_app.is_modified = FALSE;
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

static BOOL OpenImageFile(const char *path)
{
    image_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (!App_ConfirmDiscard())
        return FALSE;
    if (!path || !path[0])
        return FALSE;
    App_FlushPending();
    if (Image_Load(&loaded, path) != 0) {
        MessageBoxA(g_app.hwnd_main, "Cannot load image (PNG/JPG/BMP only).",
                    "Open", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    App_ReplaceImage(&loaded);
    g_app.is_modified = FALSE;
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
    return TRUE;
}

static BOOL App_CompareOpenPaths(char paths[CMP_MAX_CELLS][MAX_PATH],
                                 int path_count, cmp_open_mode_t mode)
{
    cmp_image_t *images[CMP_MAX_CELLS] = { NULL, NULL, NULL, NULL };
    char previous_status[sizeof(g_nav_status)];
    int count = 0, i, failures = 0, non_acp = 0, limit_reached = 0;
    HCURSOR old_cursor;
    BOOL opened = FALSE;

    if (!App_ConfirmDiscard())
        return FALSE;
    if (!paths || path_count <= 0)
        return FALSE;
    App_FlushPending();
    lstrcpynA(previous_status, g_nav_status,
              (int)sizeof(previous_status));
    if (mode == CMP_OPEN_MAIN_FIRST) {
        if (!OpenImageFile(paths[0]))
            return FALSE;
        lstrcpynA(previous_status, g_nav_status,
                  (int)sizeof(previous_status));
    }
    if (!Compare_CanOpen(1)) {
        MessageBoxA(g_app.hwnd_main,
                    "The comparison image limit (8) is reached.",
                    "Compare Files", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    strcpy(g_nav_status, "Loading comparison images...");
    App_UpdateStatus();
    UpdateWindow(g_app.hwnd_status);
    old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (mode == CMP_OPEN_WITH_CURRENT && g_app.img.valid) {
        images[count] = CmpImage_FromImage(&g_app.img);
        if (images[count])
            count++;
        else
            failures++;
    }
    for (i = 0; i < path_count && count < CMP_MAX_CELLS; i++) {
        cmp_image_t *image;
        int j;
        if (!paths[i][0] || strchr(paths[i], '?')) {
            non_acp++;
            continue;
        }
        if (mode == CMP_OPEN_WITH_CURRENT && g_app.img.valid &&
            lstrcmpiA(paths[i], g_app.img.path) == 0)
            continue;
        for (j = 0; j < count; j++)
            if (lstrcmpiA(paths[i], images[j]->img.path) == 0)
                break;
        if (j < count)
            continue;
        if (!Compare_CanOpen(1)) {
            limit_reached = 1;
            break;
        }
        image = CmpImage_Load(paths[i]);
        if (!image) {
            failures++;
            continue;
        }
        images[count++] = image;
    }
    if (i < path_count && count >= CMP_MAX_CELLS)
        limit_reached = 1;
    SetCursor(old_cursor);
    lstrcpynA(g_nav_status, previous_status, (int)sizeof(g_nav_status));
    App_UpdateStatus();
    if (count >= 2) {
        if (!CompareV1_Open(images, count))
            MessageBoxA(g_app.hwnd_main,
                        "Could not create the comparison window.",
                        "Compare Files", MB_OK | MB_ICONERROR);
        else
            opened = TRUE;
    } else if (mode == CMP_OPEN_WITH_CURRENT || path_count > 1) {
        MessageBoxA(g_app.hwnd_main,
                    "Select at least two readable images (the current image is included automatically).",
                    "Compare Files", MB_OK | MB_ICONINFORMATION);
    }
    for (i = 0; i < count; i++)
        CmpImage_Unref(images[i]);
    if (failures || non_acp) {
        char message[160];
        _snprintf(message, sizeof(message),
                  "Skipped %d unreadable and %d non-ACP path(s).",
                  failures, non_acp);
        message[sizeof(message) - 1] = '\0';
        MessageBoxA(g_app.hwnd_main, message, "Compare Files",
                    MB_OK | MB_ICONINFORMATION);
    }
    if (limit_reached)
        MessageBoxA(g_app.hwnd_main,
                    "Only four images can be compared at once, and the process-wide limit is eight images.",
                    "Compare Files", MB_OK | MB_ICONINFORMATION);
    return opened;
}

static void App_OnDropFiles(HDROP drop)
{
    char paths[CMP_MAX_CELLS][MAX_PATH];
    cmp_drop_stats_t stats;
    BOOL include_current = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL collected;
    if (!App_ConfirmDiscard()) {
        DragFinish(drop);
        return;
    }
    ZeroMemory(paths, sizeof(paths));
    collected = CmpDrop_Collect(drop, paths, CMP_MAX_CELLS, &stats);
    DragFinish(drop);
    if (!collected) {
        MessageBoxA(g_app.hwnd_main, stats.allocation_failed ?
                    "Could not collect the dropped image paths." :
                    "No supported ANSI image paths were found in the drop.",
                    "Open", MB_OK | (stats.allocation_failed ?
                    MB_ICONERROR : MB_ICONINFORMATION));
        return;
    }
    App_CompareOpenPaths(paths, (int)stats.collected,
        include_current ? CMP_OPEN_WITH_CURRENT : CMP_OPEN_MAIN_FIRST);
    if (!include_current && stats.collected >= 1) {
        char status[sizeof(g_nav_status)];
        lstrcpynA(status, g_nav_status, (int)sizeof(status));
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "%s; Ctrl+drop to include current image", status);
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
        App_UpdateStatus();
    }
    if (stats.directories || stats.unsupported || stats.non_acp ||
        stats.duplicates || stats.truncated) {
        char message[192];
        _snprintf(message, sizeof(message),
                  "Drop: %u image(s), %u folder(s), %u unsupported, "
                  "%u non-ACP, %u duplicate, %u beyond the four-image limit.",
                  stats.collected, stats.directories, stats.unsupported,
                  stats.non_acp, stats.duplicates, stats.truncated);
        message[sizeof(message) - 1] = '\0';
        MessageBoxA(g_app.hwnd_main, message, "Open",
                    MB_OK | MB_ICONINFORMATION);
    }
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

static int compare_parse_selection(const char *buffer,
                                   char paths[CMP_MAX_CELLS][MAX_PATH],
                                   int capacity, int *overflow)
{
    const char *name, *directory;
    int count = 0, length;
    if (!buffer || !buffer[0] || capacity <= 0)
        return 0;
    if (overflow)
        *overflow = 0;
    directory = buffer;
    length = (int)strlen(directory);
    name = directory + length + 1;
    if (!name[0]) {
        if (length >= MAX_PATH) {
            if (overflow)
                *overflow = 1;
            return 0;
        }
        lstrcpynA(paths[count++], directory, MAX_PATH);
        return count;
    }
    while (*name) {
        if (count < capacity) {
            int result = snprintf(paths[count], MAX_PATH, "%s%s%s", directory,
                                  (length > 0 &&
                                   (directory[length - 1] == '\\' ||
                                    directory[length - 1] == '/')) ? "" : "\\",
                                  name);
            if (result > 0 && result < MAX_PATH)
                count++;
        } else if (overflow) {
            (*overflow)++;
        }
        name += strlen(name) + 1;
    }
    return count;
}

static void CompareFilesDialog(void)
{
    char selection[32768] = { 0 };
    char paths[CMP_MAX_CELLS][MAX_PATH];
    OPENFILENAMEA ofn;
    char initial_directory[MAX_PATH];
    int path_count, selection_overflow = 0;
    if (!Compare_CanOpen(1)) {
        MessageBoxA(g_app.hwnd_main, "The comparison image limit (8) is reached.",
                    "Compare Files", MB_OK | MB_ICONWARNING);
        return;
    }
    initial_directory[0] = '\0';
    if (g_app.img.valid && g_app.img.path[0]) {
        lstrcpynA(initial_directory, g_app.img.path, MAX_PATH);
        if (!PathRemoveFileSpecA(initial_directory))
            initial_directory[0] = '\0';
    }
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd_main;
    ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
    ofn.lpstrFile = selection;
    ofn.nMaxFile = (DWORD)sizeof(selection);
    ofn.lpstrInitialDir = initial_directory[0] ? initial_directory : NULL;
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) {
        DWORD error = CommDlgExtendedError();
        if (error == FNERR_BUFFERTOOSMALL)
            MessageBoxA(g_app.hwnd_main,
                        "The selection contains too many files. Select fewer images.",
                        "Compare Files", MB_OK | MB_ICONWARNING);
        return;
    }
    path_count = compare_parse_selection(selection, paths, CMP_MAX_CELLS,
                                         &selection_overflow);
    if (path_count > 0)
        App_CompareOpenPaths(paths, path_count, CMP_OPEN_WITH_CURRENT);
    if (selection_overflow)
        MessageBoxA(g_app.hwnd_main,
                    "Only four images can be compared at once, and the process-wide limit is eight images.",
                    "Compare Files", MB_OK | MB_ICONINFORMATION);
}

static void CompareCurrentWithNext(void)
{
    cmp_image_t *current = NULL, *next = NULL;
    BOOL exact = FALSE;
    int current_index, target;
    char path[MAX_PATH];
    char previous_status[sizeof(g_nav_status)];
    if (!g_app.img.valid) {
        MessageBeep(MB_ICONWARNING);
        MessageBoxA(g_app.hwnd_main, "Open an image first.",
                    "Compare Current with Next", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_app.drag.dragging)
        return;
    if (!Compare_CanOpen(2)) {
        MessageBoxA(g_app.hwnd_main, "The comparison image limit (8) is reached.",
                    "Compare Current with Next", MB_OK | MB_ICONWARNING);
        return;
    }
    App_FlushPending();
    if (!FileList_Refresh(&g_app.files, g_app.img.path)) {
        MessageBoxA(g_app.hwnd_main, "Could not scan the current image folder.",
                    "Compare Current with Next", MB_OK | MB_ICONWARNING);
        return;
    }
    current_index = FileList_Find(&g_app.files,
                                  image_basename(g_app.img.path), &exact);
    if (current_index < 0 || g_app.files.count < 2) {
        MessageBeep(MB_ICONWARNING);
        MessageBoxA(g_app.hwnd_main, "There is no other image in this folder.",
                    "Compare Current with Next", MB_OK | MB_ICONINFORMATION);
        return;
    }
    target = exact ? current_index + 1 : current_index;
    if (target >= g_app.files.count)
        target = current_index - 1;
    if (target < 0 || target >= g_app.files.count ||
        !FileList_Path(&g_app.files, target, path)) {
        MessageBoxA(g_app.hwnd_main, "The next image path is not representable in the system code page.",
                    "Compare Current with Next", MB_OK | MB_ICONWARNING);
        return;
    }
    current = CmpImage_FromImage(&g_app.img);
    if (!current) {
        MessageBoxA(g_app.hwnd_main, "Could not copy the current image.",
                    "Compare Current with Next", MB_OK | MB_ICONERROR);
        return;
    }
    lstrcpynA(previous_status, g_nav_status, (int)sizeof(previous_status));
    if (g_app.files.cloud[target]) {
        strcpy(g_nav_status, "Cloud file downloading...");
        App_UpdateStatus();
        UpdateWindow(g_app.hwnd_status);
    }
    if (Compare_CanOpen(1))
        next = CmpImage_Load(path);
    lstrcpynA(g_nav_status, previous_status, (int)sizeof(g_nav_status));
    App_UpdateStatus();
    if (!next) {
        CmpImage_Unref(current);
        MessageBoxA(g_app.hwnd_main, "Could not load the next image.",
                    "Compare Current with Next", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!CompareV2_Open(current, next))
        MessageBoxA(g_app.hwnd_main, "Could not create the comparison window.",
                    "Compare Current with Next", MB_OK | MB_ICONERROR);
    CmpImage_Unref(current);
    CmpImage_Unref(next);
}

typedef enum {
    PROMPT_ACTION_CANCEL = 0,
    PROMPT_ACTION_RENAME = 10,
    PROMPT_ACTION_RENAME_OPEN,
    PROMPT_ACTION_COMPARE_ADD,
    PROMPT_ACTION_COMPARE_NOW
} prompt_action_t;

typedef struct {
    const char *path;
    char new_name[MAX_PATH];
    image_t thumbnail;
} new_file_prompt_t;

static void prompt_draw_thumbnail(DRAWITEMSTRUCT *draw,
                                  const image_t *image)
{
    RECT rc;
    BITMAPINFO info;
    int width, height;
    double scale;
    int draw_width, draw_height, x, y;

    if (!draw)
        return;
    rc = draw->rcItem;
    FillRect(draw->hDC, &rc, GetSysColorBrush(COLOR_WINDOW));
    if (!image || !image->valid)
        return;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    scale = (double)width / image->w;
    if ((double)height / image->h < scale)
        scale = (double)height / image->h;
    draw_width = (int)(image->w * scale);
    draw_height = (int)(image->h * scale);
    x = rc.left + (width - draw_width) / 2;
    y = rc.top + (height - draw_height) / 2;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = image->w;
    info.bmiHeader.biHeight = -image->h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(draw->hDC, HALFTONE);
    StretchDIBits(draw->hDC, x, y, draw_width, draw_height, 0, 0,
                  image->w, image->h, image->px, &info, DIB_RGB_COLORS,
                  SRCCOPY);
}

static INT_PTR CALLBACK new_file_prompt_proc(HWND dialog, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    static const int compare_buttons[] = {
        IDC_PROMPT_BTN_COMPARE_ADD, IDC_PROMPT_BTN_COMPARE_NOW
    };
    new_file_prompt_t *prompt =
        (new_file_prompt_t *)GetWindowLongPtrA(dialog, GWLP_USERDATA);
    int i;

    if (message == WM_INITDIALOG) {
        char title[MAX_PATH + 48];
        char default_name[MAX_PATH];
        char prefix[MAX_PATH];
        const char *base;
        const char *extension;
        size_t prefix_length, base_length;

        prompt = (new_file_prompt_t *)lparam;
        SetWindowLongPtrA(dialog, GWLP_USERDATA, (LONG_PTR)prompt);
        _snprintf(title, sizeof(title), "New image detected: %s", prompt->path);
        title[sizeof(title) - 1] = '\0';
        SetWindowTextA(dialog, title);
        base = image_basename(prompt->path);
        extension = PathFindExtensionA(base);
        base_length = extension ? (size_t)(extension - base) : strlen(base);
        prefix[0] = '\0';
        if (!s_persistent_prefix[0])
            Settings_LoadLastRenamePrefix(s_persistent_prefix,
                                          sizeof(s_persistent_prefix));
        strncpy(prefix, s_persistent_prefix, sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
        prefix_length = strlen(prefix);
        if (prefix_length >= sizeof(default_name))
            prefix_length = sizeof(default_name) - 1;
        memcpy(default_name, prefix, prefix_length);
        if (base_length > sizeof(default_name) - prefix_length - 1)
            base_length = sizeof(default_name) - prefix_length - 1;
        memcpy(default_name + prefix_length, base, base_length);
        default_name[prefix_length + base_length] = '\0';
        SetDlgItemTextA(dialog, IDC_PROMPT_NAME, default_name);
        if (Image_LoadWIC(&prompt->thumbnail, prompt->path) != 0)
            OutputDebugStringA("ROI Analyzer: could not load new-file thumbnail.\n");
        for (i = 0; i < (int)(sizeof(compare_buttons) /
                              sizeof(compare_buttons[0])); i++)
            EnableWindow(GetDlgItem(dialog, compare_buttons[i]),
                         g_app.img.valid);
        SetFocus(GetDlgItem(dialog, IDC_PROMPT_NAME));
        SendDlgItemMessageA(dialog, IDC_PROMPT_NAME, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message == WM_DRAWITEM) {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
        if (draw && draw->CtlID == IDC_PROMPT_THUMB) {
            prompt_draw_thumbnail(draw, prompt ? &prompt->thumbnail : NULL);
            return TRUE;
        }
    }
    if (message == WM_COMMAND) {
        int id = LOWORD(wparam);
        int action = PROMPT_ACTION_CANCEL;
        char error[160];
        if (id == IDC_PROMPT_BTN_RENAME)
            action = PROMPT_ACTION_RENAME;
        else if (id == IDC_PROMPT_BTN_RENAME_OPEN)
            action = PROMPT_ACTION_RENAME_OPEN;
        else if (id == IDC_PROMPT_BTN_COMPARE_ADD)
            action = PROMPT_ACTION_COMPARE_ADD;
        else if (id == IDC_PROMPT_BTN_COMPARE_NOW)
            action = PROMPT_ACTION_COMPARE_NOW;
        else if (id == IDCANCEL)
            EndDialog(dialog, PROMPT_ACTION_CANCEL);
        if (action != PROMPT_ACTION_CANCEL) {
            GetDlgItemTextA(dialog, IDC_PROMPT_NAME, prompt->new_name,
                            MAX_PATH);
            if (!Rename_ValidateFileName(prompt->new_name, error,
                                         sizeof(error))) {
                MessageBoxA(dialog, error, "Rename Image",
                            MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_PROMPT_NAME));
                return TRUE;
            }
            EndDialog(dialog, action);
        }
        return TRUE;
    }
    if (message == WM_DESTROY && prompt)
        Image_Free(&prompt->thumbnail);
    return FALSE;
}

static BOOL App_BuildRenamedPath(const char *old_path, const char *new_name,
                                 char new_path[MAX_PATH])
{
    char directory[MAX_PATH], full_path[MAX_PATH];
    const char *extension;
    size_t length;
    DWORD full_path_length;
    int written;
    full_path_length = GetFullPathNameA(old_path, MAX_PATH, full_path, NULL);
    if (full_path_length == 0 || full_path_length >= (DWORD)MAX_PATH)
        return FALSE;
    extension = PathFindExtensionA(full_path);
    strncpy(directory, full_path, sizeof(directory) - 1);
    directory[sizeof(directory) - 1] = '\0';
    if (!PathRemoveFileSpecA(directory))
        return FALSE;
    length = strlen(directory);
    written = _snprintf(new_path, MAX_PATH, "%s%s%s%s", directory,
                        length && directory[length - 1] != '\\' &&
                        directory[length - 1] != '/' ? "\\" : "",
                        new_name, extension ? extension : "");
    new_path[MAX_PATH - 1] = '\0';
    return written >= 0 && written < MAX_PATH;
}

static void App_SaveRenamePrefix(const char *new_path, const char *old_path)
{
    char new_base[MAX_PATH], old_base[MAX_PATH], prefix[MAX_PATH];
    const char *new_name = image_basename(new_path);
    const char *old_name = image_basename(old_path);
    const char *new_ext = PathFindExtensionA(new_name);
    const char *old_ext = PathFindExtensionA(old_name);
    size_t new_length = new_ext ? (size_t)(new_ext - new_name) :
                                  strlen(new_name);
    size_t old_length = old_ext ? (size_t)(old_ext - old_name) :
                                  strlen(old_name);
    if (new_length >= sizeof(new_base))
        new_length = sizeof(new_base) - 1;
    if (old_length >= sizeof(old_base))
        old_length = sizeof(old_base) - 1;
    memcpy(new_base, new_name, new_length);
    new_base[new_length] = '\0';
    memcpy(old_base, old_name, old_length);
    old_base[old_length] = '\0';
    if (ExtractPrefix(new_base, old_base, prefix, sizeof(prefix)) &&
        prefix[0]) {
        strncpy(s_persistent_prefix, prefix, sizeof(s_persistent_prefix) - 1);
        s_persistent_prefix[sizeof(s_persistent_prefix) - 1] = '\0';
        if (!Settings_SaveLastRenamePrefix(s_persistent_prefix))
            MessageBoxA(g_app.hwnd_main,
                        "The file was renamed, but the remembered prefix could not be saved.",
                        "Rename Image", MB_OK | MB_ICONWARNING);
    }
}

static void App_RefreshListAfterRename(const char *path)
{
    char folder[MAX_PATH];
    size_t length;
    BOOL exact = FALSE;
    strncpy(folder, path, sizeof(folder) - 1);
    folder[sizeof(folder) - 1] = '\0';
    if (!PathRemoveFileSpecA(folder) || !g_app.files.dir[0])
        return;
    length = strlen(folder);
    if (length && folder[length - 1] != '\\' && folder[length - 1] != '/') {
        if (length + 1 >= sizeof(folder))
            return;
        folder[length++] = '\\';
        folder[length] = '\0';
    }
    if (_stricmp(folder, g_app.files.dir) != 0)
        return;
    if (FileList_Refresh(&g_app.files, path)) {
        g_app.file_idx = g_app.img.valid ?
            FileList_Find(&g_app.files, image_basename(g_app.img.path), &exact) :
            -1;
        g_current_exact = g_app.img.valid && exact;
    } else {
        g_app.file_idx = -1;
        g_current_exact = FALSE;
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "Renamed; could not refresh current folder");
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
    }
    UpdateTitle();
    App_StatusSetIndex();
    App_UpdateStatus();
}

static BOOL App_RenameArrival(const char *old_path, const char *new_name,
                              char new_path[MAX_PATH])
{
    char error[160];
    DWORD result;
    BOOL overwrite = FALSE;

    if (!Rename_ValidateFileName(new_name, error, sizeof(error))) {
        MessageBoxA(g_app.hwnd_main, error, "Rename Image",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    if (!App_BuildRenamedPath(old_path, new_name, new_path)) {
        MessageBoxA(g_app.hwnd_main, "The new file path is too long.",
                    "Rename Image", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    if (_stricmp(old_path, new_path) == 0) {
        MessageBoxA(g_app.hwnd_main, "The file name is unchanged.",
                    "Rename Image", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    if (GetFileAttributesA(new_path) != INVALID_FILE_ATTRIBUTES) {
        if (MessageBoxA(g_app.hwnd_main,
                        "Target file already exists. Overwrite?",
                        "Rename Image", MB_YESNO | MB_ICONWARNING) != IDYES)
            return FALSE;
        overwrite = TRUE;
    }
    Monitor_NoteSelfRename(new_path);
    result = Rename_Execute(old_path, new_name, new_path, MAX_PATH, overwrite);
    if (result != ERROR_SUCCESS) {
        char message[192];
        _snprintf(message, sizeof(message),
                  "Could not rename file (Windows error %lu).",
                  (unsigned long)result);
        message[sizeof(message) - 1] = '\0';
        MessageBoxA(g_app.hwnd_main, message, "Rename Image",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }
    App_SaveRenamePrefix(new_path, old_path);
    return TRUE;
}

static void App_ShowNewFilePrompt(const char *path)
{
    new_file_prompt_t prompt;
    char new_path[MAX_PATH];
    int action;

    if (Monitor_IsSelfRename(path))
        return;
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return;
    ZeroMemory(&prompt, sizeof(prompt));
    prompt.path = path;
    action = (int)DialogBoxParamA(GetModuleHandleA(NULL),
                                  MAKEINTRESOURCEA(IDD_NEW_FILE_PROMPT),
                                  g_app.hwnd_main, new_file_prompt_proc,
                                  (LPARAM)&prompt);
    if (action == PROMPT_ACTION_CANCEL || action == -1)
        return;
    if (!App_RenameArrival(path, prompt.new_name, new_path))
        return;
    App_RefreshListAfterRename(new_path);
    if (action == PROMPT_ACTION_RENAME_OPEN) {
        OpenImageFile(new_path);
    } else if (action == PROMPT_ACTION_COMPARE_ADD) {
        char paths[CMP_MAX_CELLS][MAX_PATH];
        ZeroMemory(paths, sizeof(paths));
        lstrcpynA(paths[0], new_path, MAX_PATH);
        App_CompareOpenPaths(paths, 1, CMP_OPEN_WITH_CURRENT);
    } else if (action == PROMPT_ACTION_COMPARE_NOW) {
        if (!Compare_CanOpen(2)) {
            MessageBoxA(g_app.hwnd_main,
                        "The comparison image limit (8) is reached.",
                        "Compare Files", MB_OK | MB_ICONWARNING);
            return;
        }
        cmp_image_t *current = CmpImage_FromImage(&g_app.img);
        cmp_image_t *added = CmpImage_Load(new_path);
        if (!current || !added) {
            MessageBoxA(g_app.hwnd_main, "Could not load images for comparison.",
                        "Compare Files", MB_OK | MB_ICONERROR);
        } else if (!CompareV2_Open(current, added)) {
            MessageBoxA(g_app.hwnd_main,
                        "Could not create the comparison window.",
                        "Compare Files", MB_OK | MB_ICONERROR);
        }
        CmpImage_Unref(current);
        CmpImage_Unref(added);
    }
}

typedef struct {
    char name[MAX_PATH];
} rename_dialog_data_t;

static INT_PTR CALLBACK rename_input_proc(HWND dialog, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    rename_dialog_data_t *data =
        (rename_dialog_data_t *)GetWindowLongPtrA(dialog, GWLP_USERDATA);
    if (message == WM_INITDIALOG) {
        data = (rename_dialog_data_t *)lparam;
        SetWindowLongPtrA(dialog, GWLP_USERDATA, (LONG_PTR)data);
        SetDlgItemTextA(dialog, IDC_RENAME_EDIT, data->name);
        SetFocus(GetDlgItem(dialog, IDC_RENAME_EDIT));
        SendDlgItemMessageA(dialog, IDC_RENAME_EDIT, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    if (LOWORD(wparam) == IDOK) {
        char error[160];
        GetDlgItemTextA(dialog, IDC_RENAME_EDIT, data->name, MAX_PATH);
        if (!Rename_ValidateFileName(data->name, error, sizeof(error))) {
            MessageBoxA(dialog, error, "Rename Image",
                        MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog, IDC_RENAME_EDIT));
            return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void App_PerformRename(void)
{
    char old_path[MAX_PATH], new_path[MAX_PATH], default_name[MAX_PATH];
    char old_base[MAX_PATH], prefix[MAX_PATH], error[160];
    const char *file_name, *extension;
    size_t base_length, prefix_length;
    rename_dialog_data_t dialog_data;
    int dialog_result;
    DWORD rename_result;
    BOOL overwrite = FALSE;

    if (!g_app.img.valid || !g_app.img.path[0]) {
        MessageBoxA(g_app.hwnd_main, "No current image to rename.",
                    "Rename Image", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (GetFileAttributesA(g_app.img.path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(g_app.hwnd_main,
                    "Current image file not found on disk.",
                    "Rename Image", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!App_ConfirmDiscard())
        return;
    Compare_CloseAll();
    if (g_app.drag.dragging) {
        g_app.drag.dragging = FALSE;
        ReleaseCapture();
    }
    file_name = image_basename(g_app.img.path);
    extension = PathFindExtensionA(file_name);
    base_length = extension ? (size_t)(extension - file_name) :
                              strlen(file_name);
    if (base_length >= sizeof(old_base))
        base_length = sizeof(old_base) - 1;
    memcpy(old_base, file_name, base_length);
    old_base[base_length] = '\0';
    prefix[0] = '\0';
    if (!Settings_LoadLastRenamePrefix(prefix, sizeof(prefix)))
        prefix[0] = '\0';
    prefix_length = strlen(prefix);
    if (prefix_length > sizeof(default_name) - 1)
        prefix_length = sizeof(default_name) - 1;
    memcpy(default_name, prefix, prefix_length);
    if (base_length > sizeof(default_name) - prefix_length - 1)
        base_length = sizeof(default_name) - prefix_length - 1;
    memcpy(default_name + prefix_length, old_base, base_length);
    default_name[prefix_length + base_length] = '\0';
    lstrcpynA(dialog_data.name, default_name, MAX_PATH);
    dialog_result = (int)DialogBoxParamA(GetModuleHandleA(NULL),
                                         MAKEINTRESOURCEA(IDD_RENAME_INPUT),
                                         g_app.hwnd_main, rename_input_proc,
                                         (LPARAM)&dialog_data);
    if (dialog_result != IDOK ||
        strcmp(dialog_data.name, old_base) == 0)
        return;
    strncpy(old_path, g_app.img.path, sizeof(old_path) - 1);
    old_path[sizeof(old_path) - 1] = '\0';
    if (!App_BuildRenamedPath(old_path, dialog_data.name, new_path)) {
        MessageBoxA(g_app.hwnd_main, "The new file path is too long.",
                    "Rename Image", MB_OK | MB_ICONWARNING);
        return;
    }
    if (GetFileAttributesA(new_path) != INVALID_FILE_ATTRIBUTES) {
        if (MessageBoxA(g_app.hwnd_main,
                        "Target file already exists. Overwrite?",
                        "Rename Image", MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
        overwrite = TRUE;
    }

    App_UnloadImage();
    ViewPyr_Free(&g_app.pyramid);
    g_app.pyramid_attempted = FALSE;
    g_app.pyramid_pending = FALSE;
    ROI_Clear(&g_app.rois, &g_app.drag);
    Table_Clear(g_app.hwnd_table);
    HistPanel_ClearSource(g_app.hwnd_hist);
    g_app.is_modified = FALSE;
    g_app.analysis_stale = FALSE;
    g_app.file_idx = -1;
    g_current_exact = FALSE;
    InvalidateRect(g_app.hwnd_canvas, NULL, TRUE);
    UpdateWindow(g_app.hwnd_canvas);

    Monitor_NoteSelfRename(new_path);
    rename_result = Rename_Execute(old_path, dialog_data.name, new_path,
                                   sizeof(new_path), overwrite);
    if (rename_result != ERROR_SUCCESS) {
        _snprintf(error, sizeof(error),
                  "Could not rename file (Windows error %lu).",
                  (unsigned long)rename_result);
        error[sizeof(error) - 1] = '\0';
        MessageBoxA(g_app.hwnd_main, error, "Rename Image",
                    MB_OK | MB_ICONERROR);
        OpenImageFile(old_path);
        return;
    }
    App_SaveRenamePrefix(new_path, old_path);
    if (OpenImageFile(new_path)) {
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  "Renamed %s to %s", image_basename(old_path),
                  image_basename(new_path));
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
        UpdateTitle();
        App_UpdateStatus();
    }
}

static void App_HandleNewFileArrival(const char *path)
{
    HANDLE mutex;
    DWORD wait_result;
    char *copy;
    BOOL queue_full = FALSE;
    BOOL allocation_failed = FALSE;
    BOOL schedule_prompt;

    if (!path || !path[0])
        return;
    mutex = CreateMutexA(NULL, FALSE, "RoiAnalyzer_NewFileMutex");
    if (!mutex) {
        OutputDebugStringA("ROI Analyzer: could not create the new-file mutex.\n");
        return;
    }
    wait_result = WaitForSingleObject(mutex, 0);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
        if (wait_result == WAIT_FAILED)
            OutputDebugStringA("ROI Analyzer: could not acquire the new-file mutex.\n");
        CloseHandle(mutex);
        return;
    }
    schedule_prompt = !s_prompt_open;
    if (s_pending_count == PROMPT_QUEUE_CAPACITY) {
        s_dropped_files++;
        queue_full = TRUE;
    } else {
        copy = _strdup(path);
        if (!copy) {
            OutputDebugStringA("ROI Analyzer: could not queue a monitored file.\n");
            s_dropped_files++;
            allocation_failed = TRUE;
        } else {
            size_t tail = (s_pending_head + s_pending_count) %
                          PROMPT_QUEUE_CAPACITY;
            s_pending_files[tail] = copy;
            s_pending_count++;
        }
    }
    if (!ReleaseMutex(mutex))
        OutputDebugStringA("ROI Analyzer: could not release the new-file mutex.\n");
    CloseHandle(mutex);
    if (queue_full || allocation_failed) {
        _snprintf(g_nav_status, sizeof(g_nav_status),
                  queue_full ? "Monitor queue full; %u new file(s) skipped" :
                               "Monitor could not queue file; %u skipped",
                  s_dropped_files);
        g_nav_status[sizeof(g_nav_status) - 1] = '\0';
        App_UpdateStatus();
    }
    if (schedule_prompt && s_pending_count)
        App_SchedulePromptQueue();
}

static void App_SchedulePromptQueue(void)
{
    if (!s_pending_count || s_prompt_open)
        return;
    if (!PostMessageA(g_app.hwnd_main, WM_APP_DRAIN_NEW_FILES, 0, 0)) {
        OutputDebugStringA("ROI Analyzer: could not schedule the pending file prompt.\n");
        if (!SetTimer(g_app.hwnd_main, PROMPT_DRAIN_TIMER, 250, NULL))
            OutputDebugStringA("ROI Analyzer: could not start the prompt retry timer.\n");
    }
}

static void App_DrainPromptQueue(void)
{
    char *path;

    if (s_prompt_open || s_pending_count == 0)
        return;
    if (!IsWindowEnabled(g_app.hwnd_main)) {
        SetTimer(g_app.hwnd_main, PROMPT_DRAIN_TIMER, 250, NULL);
        return;
    }
    KillTimer(g_app.hwnd_main, PROMPT_DRAIN_TIMER);
    path = s_pending_files[s_pending_head];
    s_pending_files[s_pending_head] = NULL;
    s_pending_head = (s_pending_head + 1) % PROMPT_QUEUE_CAPACITY;
    s_pending_count--;
    s_prompt_open = TRUE;
    App_ShowNewFilePrompt(path);
    free(path);
    s_prompt_open = FALSE;
    if (s_pending_count)
        App_SchedulePromptQueue();
}

static void ExportCurrent(void)
{
    char paths[3][MAX_PATH];
    char error_path[MAX_PATH] = "";
    char status[512] = "Exported:";
    int i, used = (int)strlen(status), export_result;
    static const roi_mode_t modes[] = { MODE_DRAG, MODE_GRID3, MODE_GRID5 };
    if (g_app.rois.count == 0) {
        MessageBoxA(g_app.hwnd_main, "尚無 ROI", "Export",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    errno = 0;
    export_result = g_app.img.valid ?
                    Export_Log(&g_app.img, &g_app.rois, error_path,
                               sizeof(error_path)) : -1;
    if (export_result != 0) {
        if (errno == EACCES)
            MessageBoxA(g_app.hwnd_main,
                        "Could not write the CSV export. The file may be locked by another program (such as Excel). Close it and try again.",
                        "Export", MB_OK | MB_ICONERROR);
        else if (errno == EILSEQ) {
            char message[512];

            if (error_path[0]) {
                _snprintf(message, sizeof(message),
                          "匯出檔為舊格式（非 Unicode），為避免損壞無法續寫。請改用新檔名或先移除舊檔後再試。\n%s",
                          error_path);
                message[sizeof(message) - 1] = '\0';
                MessageBoxA(g_app.hwnd_main, message, "Export",
                            MB_OK | MB_ICONERROR);
            } else {
                MessageBoxA(g_app.hwnd_main,
                            "匯出檔為舊格式（非 Unicode），為避免損壞無法續寫。請改用新檔名或先移除舊檔後再試。",
                            "Export", MB_OK | MB_ICONERROR);
            }
        } else if (errno == EINVAL) {
            MessageBoxA(g_app.hwnd_main,
                        "The CSV export file is incomplete and cannot be appended. Remove it or choose a new filename.",
                        "Export", MB_OK | MB_ICONERROR);
        } else {
            MessageBoxA(g_app.hwnd_main, "Could not open or write the CSV export.",
                        "Export", MB_OK | MB_ICONERROR);
        }
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

static void OpenCsvFile(void)
{
    char path[MAX_PATH];
    if (!g_app.img.valid) {
        MessageBoxA(g_app.hwnd_main, "No current image.", "CSV",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (Export_GetPath(&g_app.img, g_app.mode, path, sizeof(path)) == 0 &&
        GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        if ((INT_PTR)ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) <= 32)
            MessageBoxA(g_app.hwnd_main, "Could not open the CSV file.", "CSV",
                        MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxA(g_app.hwnd_main, "The current image has no CSV file.",
                "CSV", MB_OK | MB_ICONINFORMATION);
}

static void OpenImageFolder(void)
{
    char folder[MAX_PATH];
    if (!g_app.img.valid) {
        MessageBoxA(g_app.hwnd_main, "No current image.", "Log",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    strncpy(folder, g_app.img.path, sizeof(folder) - 1);
    folder[sizeof(folder) - 1] = '\0';
    if (!PathRemoveFileSpecA(folder))
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
        (void)Detect_Init(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        Layout();
        App_UpdateStatus();
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
    case WM_INITMENUPOPUP:
        App_UpdateImageMenu();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam);
        App_FlushPending();
        if (id == IDM_OPEN)
            OpenImageDialog();
        else if (id == IDM_EXPORT || id == IDC_EXPORT)
            ExportCurrent();
        else if (id == IDM_EXIT)
            SendMessageA(hwnd, WM_CLOSE, 0, 0);
        else if (id == IDM_SAVE_IMAGE)
            App_SaveImage();
        else if (id == IDM_RENAME_FILE)
            App_PerformRename();
        else if (id == IDM_MONITOR_SETTINGS)
            Monitor_ShowSettingsDialog(hwnd);
        else if (id == IDM_COMPARE_METRICS) {
            if (!Compare_TriggerMetrics())
                MessageBoxA(hwnd, "Open a comparison window before requesting "
                            "a metrics report.", "Metrics Report",
                            MB_OK | MB_ICONINFORMATION);
        }
        else if (id == IDM_METRICS_SETTINGS) {
            Compare_SetMetricsStage2Enabled(
                !Compare_MetricsStage2Enabled());
            CheckMenuItem(g_menu_view, IDM_METRICS_SETTINGS,
                          MF_BYCOMMAND |
                          (Compare_MetricsStage2Enabled() ?
                           MF_CHECKED : MF_UNCHECKED));
        }
        else if (id == IDM_ROT90)
            App_RotateOrthogonal(1);
        else if (id == IDM_ROT180)
            App_RotateOrthogonal(2);
        else if (id == IDM_ROT270)
            App_RotateOrthogonal(3);
        else if (id == IDM_ROT_ANY) {
            double degrees = 0.0;
            if (DialogBoxParamA(GetModuleHandleA(NULL),
                                MAKEINTRESOURCEA(IDD_ROTATE_ANGLE), hwnd,
                                RotateAngleDlgProc, (LPARAM)&degrees) == IDOK &&
                fmod(degrees, 360.0) != 0.0)
                App_RotateArbitrary(degrees);
        }
        else if (id == IDM_PREVIOUS)
            App_Navigate(-1, FALSE);
        else if (id == IDM_NEXT)
            App_Navigate(1, FALSE);
        else if (id == IDM_COMPARE_FILES)
            CompareFilesDialog();
        else if (id == IDM_COMPARE_NEXT)
            CompareCurrentWithNext();
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
        else if (id == IDM_OPENCSV)
            OpenCsvFile();
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
        App_OnDropFiles((HDROP)wparam);
        return 0;
    }
    case WM_APP_NEW_FILE: {
        char *path = (char *)lparam;
        App_HandleNewFileArrival(path);
        free(path);
        return 0;
    }
    case WM_APP_DRAIN_NEW_FILES:
        App_DrainPromptQueue();
        return 0;
    case WM_APP_DETECT_INIT:
        Detect_OnInitResult((detect_init_result_t *)lparam);
        App_UpdateStatus();
        return 0;
    case WM_APP_DETECT_DONE:
        if (wparam == DETECT_RESULT_ALLOCATION_FAILURE)
            Detect_OnResultAllocationFailure((LONG)lparam);
        else
            Detect_OnResult((detect_result_t *)lparam);
        App_UpdateStatus();
        return 0;
    case WM_TIMER:
        if (wparam == PROMPT_DRAIN_TIMER) {
            KillTimer(hwnd, PROMPT_DRAIN_TIMER);
            App_DrainPromptQueue();
            return 0;
        }
        if (wparam == IDT_DETECT_DELAY) {
            Detect_OnTimer(g_app.img.px, g_app.img.w, g_app.img.h,
                           g_app.img.pitch);
            App_UpdateStatus();
            return 0;
        }
        break;
    case WM_CLOSE:
        if (!App_ConfirmDiscard())
            return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        Detect_Shutdown();
        Monitor_Shutdown();
        KillTimer(hwnd, PROMPT_DRAIN_TIMER);
        {
            MSG pending;
            while (PeekMessageA(&pending, hwnd, WM_APP_NEW_FILE,
                                WM_APP_NEW_FILE, PM_REMOVE))
                free((void *)pending.lParam);
            while (s_pending_count) {
                free(s_pending_files[s_pending_head]);
                s_pending_files[s_pending_head] = NULL;
                s_pending_head = (s_pending_head + 1) %
                                 PROMPT_QUEUE_CAPACITY;
                s_pending_count--;
            }
        }
        Compare_CloseAll();
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
    HMENU image = CreatePopupMenu();
    HMENU csv = CreatePopupMenu();
    HMENU view = CreatePopupMenu();

    AppendMenuA(file, MF_STRING, IDM_OPEN, "Open...\tO");
    AppendMenuA(file, MF_STRING, IDM_SAVE_IMAGE, "Save Image\tCtrl+S");
    AppendMenuA(file, MF_STRING, IDM_RENAME_FILE, "Rename File...\tF2");
    AppendMenuA(file, MF_STRING, IDM_PREVIOUS, "Previous Image");
    AppendMenuA(file, MF_STRING, IDM_NEXT, "Next Image");
    AppendMenuA(file, MF_STRING, IDM_EXPORT, "Export CSV\tCtrl+E");
    AppendMenuA(file, MF_STRING, IDM_MONITOR_SETTINGS,
                "Folder Monitor Settings...");
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
    AppendMenuA(image, MF_STRING, IDM_ROT90, "Rotate 90 CW");
    AppendMenuA(image, MF_STRING, IDM_ROT180, "Rotate 180 degrees");
    AppendMenuA(image, MF_STRING, IDM_ROT270, "Rotate 270 CW");
    AppendMenuA(image, MF_STRING, IDM_ROT_ANY, "Rotate Arbitrary...");
    AppendMenuA(csv, MF_STRING, IDM_OPENCSV, "Open CSV File");
    AppendMenuA(csv, MF_STRING, IDM_FOLDER, "Open Folder");
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_HISTOGRAM, "Histogram Panel\tH");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_HIST_RGB, "Channel: RGB\tA");
    AppendMenuA(view, MF_STRING, IDM_HIST_Y, "Luminosity (Y)\tY");
    AppendMenuA(view, MF_STRING, IDM_HIST_R, "Red\tR");
    AppendMenuA(view, MF_STRING, IDM_HIST_G, "Green\tG");
    AppendMenuA(view, MF_STRING, IDM_HIST_B, "Blue\tB");
    AppendMenuA(view, MF_STRING, IDM_HIST_LOG, "Log Scale\tL");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, IDM_COMPARE_FILES, "Compare Files...\tCtrl+K");
    AppendMenuA(view, MF_STRING, IDM_COMPARE_NEXT,
                "Compare Current with Next\tK");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, IDM_COMPARE_METRICS,
                "Metrics Report for Compare\tCtrl+M");
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_METRICS_SETTINGS,
                "Stage 2 FFT and Lab Metrics");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)mode, "Mode");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)image, "Image");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "View");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)csv, "CSV");
    g_menu_mode = mode;
    g_menu_view = view;
    g_menu_image = image;
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
        { FVIRTKEY, 'L', IDM_HIST_LOG },
        { FVIRTKEY | FCONTROL, 'S', IDM_SAVE_IMAGE },
        { FVIRTKEY | FCONTROL, 'K', IDM_COMPARE_FILES },
        { FVIRTKEY | FCONTROL, 'M', IDM_COMPARE_METRICS },
        { FVIRTKEY, 'K', IDM_COMPARE_NEXT },
        { FVIRTKEY, VK_F2, IDM_RENAME_FILE }
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
    if (!Compare_Init(instance, (HFONT)GetStockObject(DEFAULT_GUI_FONT))) {
        MessageBoxA(NULL, "Could not register the comparison window classes.",
                    "ROI Analyzer", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
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
    if (!Settings_LoadLastRenamePrefix(s_persistent_prefix,
                                       sizeof(s_persistent_prefix)))
        s_persistent_prefix[0] = '\0';
    if (!Monitor_Init(g_app.hwnd_main))
        MessageBoxA(g_app.hwnd_main,
                    "Could not initialize the folder monitor.",
                    "ROI Analyzer", MB_OK | MB_ICONWARNING);
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
        HWND root;
        if (Compare_PreTranslate(&msg)) {
            g_discard_approved = FALSE;
            continue;
        }
        root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : NULL;
        if (root == g_app.hwnd_main) {
            if (msg.message == WM_KEYUP &&
                (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT))
                App_FlushPending();
            if (msg.message == WM_KEYDOWN &&
                (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) &&
                App_NavKeyAllowed(&msg)) {
                BOOL repeat = (msg.lParam & (1L << 30)) != 0;
                if (repeat && (LONG)(msg.time - s_nav_done_tick) < 0) {
                    g_discard_approved = FALSE;
                    continue;
                }
                App_Navigate(msg.wParam == VK_LEFT ? -1 : 1, repeat);
                s_nav_done_tick = GetTickCount();
                g_discard_approved = FALSE;
                continue;
            }
            if (TranslateAcceleratorA(g_app.hwnd_main, g_accelerators, &msg)) {
                g_discard_approved = FALSE;
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        g_discard_approved = FALSE;
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
