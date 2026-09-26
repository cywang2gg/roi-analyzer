#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus/gdiplus.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "canvas.h"
#include "export.h"
#include "table.h"

#define IDM_OPEN       101
#define IDM_EXPORT     102
#define IDM_EXIT       103
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
#define IDC_CANVAS     1001
#define IDC_TABLE      1002
#define IDC_EXPORT     1003
#define IDC_CLEAR      1004
#define IDC_MULTI      1005
#define IDC_TABS       1006

app_t g_app;

static ULONG_PTR g_gdiplus;
static HACCEL g_accelerators;
static BOOL g_syncing_table;
static HMENU g_menu_mode;

static void Layout(void);
static void SetMode(roi_mode_t mode);
static void SetMulti(BOOL multi);
static void ExportCurrent(void);
static void ChangeZoom(float factor);

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
    if (g_app.img.valid) {
        _snprintf(title, sizeof(title), "ROI Analyzer - %s %dx%d [%s] [%s]",
                  image_basename(g_app.img.path), g_app.img.w, g_app.img.h,
                  ROI_ModeLabel(g_app.mode), g_app.multi ? "Multi" : "Single");
    } else {
        _snprintf(title, sizeof(title), "ROI Analyzer - (open or drop an image) [%s] [%s]",
                  ROI_ModeLabel(g_app.mode), g_app.multi ? "Multi" : "Single");
    }
    title[sizeof(title) - 1] = '\0';
    SetWindowTextA(g_app.hwnd_main, title);
}

void App_UpdateStatus(void)
{
    char text[512];
    char cursor[128] = "cursor: outside image";
    char selection[192] = "no ROI selected";
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
    if (g_app.rois.selected >= 0 && g_app.rois.selected < g_app.rois.count) {
        const roi_result_t *r = &g_app.rois.items[g_app.rois.selected].res;
        _snprintf(selection, sizeof(selection),
                  "ROI %d: rect=(%d,%d)-(%d,%d) mean RGB=(%.2f,%.2f,%.2f)",
                  ROI_SourceIndex(&g_app.rois, g_app.rois.selected) + 1,
                  r->x0, r->y0, r->x1, r->y1,
                  r->r_mean, r->g_mean, r->b_mean);
    }
    _snprintf(text, sizeof(text), "%s  |  %s  |  ROI: %d  |  Mode: %s%s  |  Zoom: %.0f%%  |  Pan: (%d,%d)",
              cursor, selection, g_app.rois.count, ROI_ModeLabel(g_app.mode),
              g_app.multi ? " (multi)" : "", g_app.view.zoom * 100.0,
              g_app.view.pan_x, g_app.view.pan_y);
    text[sizeof(text) - 1] = '\0';
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, 0, (LPARAM)text);
}

void App_RoiChanged(void)
{
    if (g_app.hwnd_table) {
        g_syncing_table = TRUE;
        if (!Table_Rebuild(g_app.hwnd_table, &g_app.rois,
                           ROI_ModeSource(g_app.table_page)))
            MessageBoxA(g_app.hwnd_main, "Could not update the ROI table.",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
        g_syncing_table = FALSE;
    }
    if (g_app.hwnd_canvas)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
    App_UpdateStatus();
}

void App_SetTablePage(roi_mode_t page)
{
    int tab = page == MODE_GRID3 ? 1 : (page == MODE_GRID5 ? 2 : 0);
    g_app.table_page = page;
    if (g_app.hwnd_tabs)
        TabCtrl_SetCurSel(g_app.hwnd_tabs, tab);
    App_RoiChanged();
}

void App_SelectROI(int global_index)
{
    if (global_index >= 0 && global_index < g_app.rois.count) {
        roi_source_t source = g_app.rois.items[global_index].source;
        g_app.rois.selected = global_index;
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
        App_RoiChanged();
        if (g_app.hwnd_table)
            Table_Select(g_app.hwnd_table, Table_FindRow(g_app.hwnd_table,
                                                         global_index));
    } else {
        g_app.rois.selected = -1;
        App_RoiChanged();
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

static void Layout(void)
{
    RECT client, status_rect;
    int width, height, status_height = 0;
    int table_height, button_height = 28, tabs_height = 28, canvas_height;
    int available;

    if (!g_app.hwnd_main || !g_app.hwnd_status || !g_app.hwnd_canvas ||
        !g_app.hwnd_table || !g_app.hwnd_tabs)
        return;
    SendMessage(g_app.hwnd_status, WM_SIZE, 0, 0);
    GetClientRect(g_app.hwnd_main, &client);
    GetWindowRect(g_app.hwnd_status, &status_rect);
    status_height = status_rect.bottom - status_rect.top;
    width = client.right;
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

    SetWindowPos(g_app.hwnd_canvas, NULL, 0, 0, width, canvas_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
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
    if (Image_Load(&loaded, path) != 0) {
        MessageBoxA(g_app.hwnd_main, "Cannot load image (PNG/JPG/BMP only).",
                    "Open", MB_OK | MB_ICONWARNING);
        return;
    }
    Image_Free(&g_app.img);
    g_app.img = loaded;
    ROI_Clear(&g_app.rois, &g_app.drag);
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
    SendMessageA(g_app.hwnd_status, SB_SETTEXTA, 0, (LPARAM)status);
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
        App_RoiChanged();
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wparam,
                                    LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        HINSTANCE instance = ((CREATESTRUCTA *)lparam)->hInstance;
        g_app.hwnd_main = hwnd;
        g_app.hwnd_status = CreateWindowExA(0, STATUSCLASSNAMEA, "",
                                             WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                             0, 0, 0, 0, hwnd, (HMENU)2000,
                                             instance, NULL);
        g_app.hwnd_canvas = CreateWindowExA(0, "RoiAnalyzerCanvas", "",
                                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                             0, 0, 0, 0, hwnd, (HMENU)IDC_CANVAS,
                                             instance, NULL);
        g_app.hwnd_btn_export = CreateWindowExA(0, "BUTTON", "Export",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  0, 0, 0, 0, hwnd,
                                                  (HMENU)IDC_EXPORT, instance, NULL);
        g_app.hwnd_btn_clear = CreateWindowExA(0, "BUTTON", "Clear",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                0, 0, 0, 0, hwnd,
                                                (HMENU)IDC_CLEAR, instance, NULL);
        g_app.hwnd_chk_multi = CreateWindowExA(0, "BUTTON", "Multi",
                                                WS_CHILD | WS_VISIBLE |
                                                BS_AUTOCHECKBOX,
                                                0, 0, 0, 0, hwnd,
                                                (HMENU)IDC_MULTI, instance, NULL);
        g_app.hwnd_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
                                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                           TCS_TABS,
                                           0, 0, 0, 0, hwnd,
                                           (HMENU)IDC_TABS, instance, NULL);
        g_app.hwnd_table = Table_Create(hwnd, instance, IDC_TABLE);
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
        if (!g_app.hwnd_status || !g_app.hwnd_canvas || !g_app.hwnd_btn_export ||
            !g_app.hwnd_btn_clear || !g_app.hwnd_chk_multi || !g_app.hwnd_table ||
            !g_app.hwnd_tabs)
            return -1;
        DragAcceptFiles(hwnd, TRUE);
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        App_UpdateStatus();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam);
        if (id == IDM_OPEN)
            OpenImageDialog();
        else if (id == IDM_EXPORT || id == IDC_EXPORT)
            ExportCurrent();
        else if (id == IDM_EXIT)
            DestroyWindow(hwnd);
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
        else if (id == 2001 && g_app.drag.dragging) {
            g_app.drag.dragging = FALSE;
            ReleaseCapture();
            InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
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
                g_app.rois.selected = Table_SelectedGlobalIndex(g_app.hwnd_table);
            if (g_app.hwnd_canvas)
                InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
            App_UpdateStatus();
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

    AppendMenuA(file, MF_STRING, IDM_OPEN, "Open...\tO");
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
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)mode, "Mode");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)log, "Log");
    g_menu_mode = mode;
    return bar;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show)
{
    INITCOMMONCONTROLSEX controls;
    WNDCLASSA wc;
    HMENU menu;
    ACCEL accelerators[] = {
        { FVIRTKEY, '1', IDM_DRAG },
        { FVIRTKEY, '2', IDM_GRID3 },
        { FVIRTKEY, '3', IDM_GRID5 },
        { FVIRTKEY, 'M', IDM_MULTI },
        { FVIRTKEY, VK_DELETE, IDM_DELETE },
        { FVIRTKEY, 'C', IDM_CLEAR },
        { FVIRTKEY | FSHIFT, 'C', IDM_CLEAR_ALL },
        { FVIRTKEY, 'O', IDM_OPEN },
        { FVIRTKEY | FCONTROL, 'E', IDM_EXPORT },
        { FVIRTKEY, VK_ADD, IDM_ZOOM_IN },
        { FVIRTKEY, VK_SUBTRACT, IDM_ZOOM_OUT },
        { FVIRTKEY, VK_OEM_PLUS, IDM_ZOOM_IN },
        { FVIRTKEY, VK_OEM_MINUS, IDM_ZOOM_OUT },
        { FVIRTKEY | FSHIFT, VK_OEM_PLUS, IDM_ZOOM_IN },
        { FVIRTKEY, VK_ESCAPE, 2001 }
    };
    MSG msg;
    int screen_width, screen_height, width, height, x, y;
    int result;

    (void)previous;
    memset(&g_app, 0, sizeof(g_app));
    g_app.mode = MODE_DRAG;
    g_app.table_page = MODE_DRAG;
    g_app.view.zoom = 1.0f;
    ROI_Init(&g_app.rois, &g_app.drag);
    {
        GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
        if (GdiplusStartup(&g_gdiplus, &input, NULL) != Ok) {
            MessageBoxA(NULL, "GDI+ initialization failed.", "ROI Analyzer",
                        MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&controls) || !Canvas_Register(instance)) {
        MessageBoxA(NULL, "Could not initialize Windows common controls.",
                    "ROI Analyzer", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplus);
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
        return 1;
    }
    menu = CreateMainMenu();
    screen_width = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);
    width = screen_width > 0 ? screen_width / 2 : 1280;
    height = screen_height > 0 ? screen_height / 2 : 800;
    x = screen_width > 0 ? screen_width / 4 : CW_USEDEFAULT;
    y = screen_height > 0 ? screen_height / 4 : CW_USEDEFAULT;
    g_app.hwnd_main = CreateWindowExA(WS_EX_ACCEPTFILES, "RoiAnalyzerMain",
                                      "ROI Analyzer", WS_OVERLAPPEDWINDOW,
                                      x, y, width, height, NULL, menu, instance, NULL);
    if (!g_app.hwnd_main) {
        MessageBoxA(NULL, "Could not create the main window.", "ROI Analyzer",
                    MB_OK | MB_ICONERROR);
        ROI_Destroy(&g_app.rois);
        GdiplusShutdown(g_gdiplus);
        return 1;
    }
    g_accelerators = CreateAcceleratorTableA(accelerators,
                            (int)(sizeof(accelerators) / sizeof(accelerators[0])));
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
    while ((result = GetMessageA(&msg, NULL, 0, 0)) > 0) {
        if (!TranslateAcceleratorA(g_app.hwnd_main, g_accelerators, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    if (g_accelerators)
        DestroyAcceleratorTable(g_accelerators);
    Image_Free(&g_app.img);
    ROI_Destroy(&g_app.rois);
    GdiplusShutdown(g_gdiplus);
    return result == -1 ? 1 : (int)msg.wParam;
}
