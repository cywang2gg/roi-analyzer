#include "compare.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V1_TOOLBAR_H 40
#define V1_ID_LOCK   4101
#define V1_ID_V2     4102
#define V1_ID_SNAPSHOT 4103
#define V1_ID_INFO   4104

typedef struct {
    cmp_image_t *image;
    cmp_view_t view;
    RECT cell;
    RECT image_rect;
    RECT status_rect;
    RECT close_rect;
} cmp_cell_t;

typedef struct {
    HWND hwnd;
    HWND grid;
    HWND lock;
    HWND v2;
    HWND snapshot;
    HWND info_bar;
    HWND message;
    HWND tooltip;
    cmp_cell_t cells[CMP_MAX_CELLS];
    int count;
    BOOL locked;
    BOOL show_info;
    BOOL need_fit;
    BOOL registered;
    int pan_source;
    int close_down;
    int hover;
    POINT last;
    int wheel_acc;
    HDC mem_dc;
    HBITMAP mem_bitmap;
    HBITMAP old_bitmap;
    int mem_w;
    int mem_h;
    double paint_ms;
} cmp_v1_t;

static const char V1_CLASS[] = "RoiCmpV1";
static const char V1_GRID_CLASS[] = "RoiCmpGrid";
BOOL CompareV2_Register(HINSTANCE instance);
void Compare_SetFont(HFONT font);
static void v1_fit_all(cmp_v1_t *state);
static void v1_snapshot(cmp_v1_t *state, BOOL copy_only);

static cmp_v1_t *v1_state(HWND hwnd)
{
    return (cmp_v1_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
}

static void v1_free_backbuffer(cmp_v1_t *state)
{
    if (state->mem_dc && state->old_bitmap)
        SelectObject(state->mem_dc, state->old_bitmap);
    if (state->mem_bitmap)
        DeleteObject(state->mem_bitmap);
    if (state->mem_dc)
        DeleteDC(state->mem_dc);
    state->mem_dc = NULL;
    state->mem_bitmap = NULL;
    state->old_bitmap = NULL;
    state->mem_w = state->mem_h = 0;
}

static void v1_release(cmp_v1_t *state)
{
    int i;
    if (!state)
        return;
    CmpReg_Remove(state->hwnd);
    state->registered = FALSE;
    v1_free_backbuffer(state);
    for (i = 0; i < state->count; i++) {
        CmpImage_Unref(state->cells[i].image);
        state->cells[i].image = NULL;
    }
    state->count = 0;
}

static void v1_update_message(cmp_v1_t *state)
{
    char text[160];
    _snprintf(text, sizeof(text), "%d images | %s | paint %.1f ms",
              state->count, state->locked ? "Lock on" : "Lock off",
              state->paint_ms);
    text[sizeof(text) - 1] = '\0';
    if (state->message)
        SetWindowTextA(state->message, text);
}

static void v1_layout(cmp_v1_t *state)
{
    RECT client;
    int width, height, columns, rows, gap = 1, i;
    if (!state->grid || !GetClientRect(state->grid, &client))
        return;
    width = client.right;
    height = client.bottom;
    columns = state->count == 2 ? 2 : (state->count == 3 ? 1 : 2);
    rows = state->count == 2 ? 1 : (state->count == 3 ? 3 : 2);
    for (i = 0; i < state->count; i++) {
        int col = i % columns;
        int row = i / columns;
        int left = col * (width + gap) / columns;
        int right = (col + 1) * (width + gap) / columns - gap;
        int top = row * (height + gap) / rows;
        int bottom = (row + 1) * (height + gap) / rows - gap;
        int band = 23;
        cmp_cell_t *cell = &state->cells[i];
        cell->cell.left = left;
        cell->cell.top = top;
        cell->cell.right = right;
        cell->cell.bottom = bottom;
        cell->status_rect.left = left;
        cell->status_rect.right = right;
        cell->status_rect.bottom = bottom;
        cell->status_rect.top = bottom - band;
        if (cell->status_rect.top < top)
            cell->status_rect.top = top;
        cell->image_rect.left = left;
        cell->image_rect.top = top;
        cell->image_rect.right = right;
        cell->image_rect.bottom = cell->status_rect.top;
        cell->close_rect.right = right - 5;
        cell->close_rect.left = cell->close_rect.right - 20;
        cell->close_rect.top = top + 5;
        cell->close_rect.bottom = cell->close_rect.top + 20;
    }
    if (state->need_fit && width > 0 && height > 0) {
        state->need_fit = FALSE;
        for (i = 0; i < state->count; i++)
            CmpView_Fit(&state->cells[i].view,
                        state->cells[i].image->img.w,
                        state->cells[i].image->img.h,
                        state->cells[i].image_rect.right -
                            state->cells[i].image_rect.left,
                        state->cells[i].image_rect.bottom -
                            state->cells[i].image_rect.top,
                        1.0);
    } else {
        for (i = 0; i < state->count; i++)
            CmpView_ClampEdges(&state->cells[i].view,
                               state->cells[i].image->img.w,
                               state->cells[i].image->img.h,
                               state->cells[i].image_rect.right -
                                   state->cells[i].image_rect.left,
                               state->cells[i].image_rect.bottom -
                                   state->cells[i].image_rect.top);
    }
}

static void v1_fit_all(cmp_v1_t *state)
{
    int i, width, height;
    double common = CMP_ZOOM_MAX;
    if (state->count <= 0)
        return;
    if (state->locked && state->count > 1) {
        for (i = 0; i < state->count; i++) {
            cmp_cell_t *cell = &state->cells[i];
            width = cell->image_rect.right - cell->image_rect.left;
            height = cell->image_rect.bottom - cell->image_rect.top;
            CmpView_Fit(&cell->view, cell->image->img.w, cell->image->img.h,
                        width, height, 1.0);
            if (cell->view.zoom < common)
                common = cell->view.zoom;
        }
        for (i = 0; i < state->count; i++) {
            cmp_cell_t *cell = &state->cells[i];
            cell->view.zoom = common;
            cell->view.u = cell->image->img.w * 0.5;
            cell->view.v = cell->image->img.h * 0.5;
            CmpView_ClampEdges(&cell->view, cell->image->img.w,
                               cell->image->img.h,
                               cell->image_rect.right - cell->image_rect.left,
                               cell->image_rect.bottom - cell->image_rect.top);
        }
    } else {
        for (i = 0; i < state->count; i++) {
            cmp_cell_t *cell = &state->cells[i];
            CmpView_Fit(&cell->view, cell->image->img.w, cell->image->img.h,
                        cell->image_rect.right - cell->image_rect.left,
                        cell->image_rect.bottom - cell->image_rect.top, 1.0);
        }
    }
    InvalidateRect(state->grid, NULL, FALSE);
}

static BOOL v1_prepare_backbuffer(cmp_v1_t *state, HDC dc, int width, int height)
{
    if (width <= 0 || height <= 0)
        return FALSE;
    if (state->mem_dc && state->mem_w == width && state->mem_h == height)
        return TRUE;
    v1_free_backbuffer(state);
    state->mem_dc = CreateCompatibleDC(dc);
    state->mem_bitmap = CreateCompatibleBitmap(dc, width, height);
    if (!state->mem_dc || !state->mem_bitmap) {
        v1_free_backbuffer(state);
        return FALSE;
    }
    state->old_bitmap = (HBITMAP)SelectObject(state->mem_dc, state->mem_bitmap);
    state->mem_w = width;
    state->mem_h = height;
    return TRUE;
}

static void v1_render(cmp_v1_t *state, HDC dc, int width, int height,
                      unsigned int flags)
{
    RECT client;
    int i;
    HBRUSH background;
    (void)flags;
    client.left = 0;
    client.top = 0;
    client.right = width;
    client.bottom = height;
    background = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 245, 245));
    SelectObject(dc, Cmp_UiFont());
    for (i = 0; i < state->count; i++) {
        cmp_cell_t *cell = &state->cells[i];
        char label[MAX_PATH + 100];
        char close_text[] = "X";
        int view_w = cell->image_rect.right - cell->image_rect.left;
        int view_h = cell->image_rect.bottom - cell->image_rect.top;
        Cmp_Blit(dc, cell->image, &cell->view,
                 &cell->image_rect, &cell->image_rect);
        FillRect(dc, &cell->status_rect,
                 (HBRUSH)GetStockObject(BLACK_BRUSH));
        _snprintf(label, sizeof(label), "%d: %s | %.0f%% | %dx%d | %dx%d",
                  i + 1, cell->image->name, cell->view.zoom * 100.0,
                  (int)(cell->image->img.w * cell->view.zoom),
                  (int)(cell->image->img.h * cell->view.zoom),
                  cell->image->img.w, cell->image->img.h);
        label[sizeof(label) - 1] = '\0';
        {
            RECT text_rect = cell->status_rect;
            text_rect.left += 5;
            text_rect.right -= 25;
            DrawTextA(dc, label, -1, &text_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (view_w > 0 && view_h > 0) {
            HBRUSH close_brush = CreateSolidBrush(RGB(150, 35, 35));
            FillRect(dc, &cell->close_rect, close_brush);
            DeleteObject(close_brush);
            DrawTextA(dc, close_text, -1, &cell->close_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                      DT_NOPREFIX);
        }
    }
    if (state->count > 1) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(130, 130, 130));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        if (state->count == 2) {
            int x = width / 2;
            MoveToEx(dc, x, 0, NULL);
            LineTo(dc, x, height);
        } else if (state->count == 3) {
            int y1 = height / 3;
            int y2 = height * 2 / 3;
            MoveToEx(dc, 0, y1, NULL);
            LineTo(dc, width, y1);
            MoveToEx(dc, 0, y2, NULL);
            LineTo(dc, width, y2);
        } else {
            int x = width / 2, y = height / 2;
            MoveToEx(dc, x, 0, NULL);
            LineTo(dc, x, height);
            MoveToEx(dc, 0, y, NULL);
            LineTo(dc, width, y);
        }
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
}

static void v1_paint(cmp_v1_t *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client;
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);
    GetClientRect(hwnd, &client);
    if (v1_prepare_backbuffer(state, dc, client.right, client.bottom)) {
        v1_render(state, state->mem_dc, client.right, client.bottom, 0);
        BitBlt(dc, 0, 0, client.right, client.bottom,
               state->mem_dc, 0, 0, SRCCOPY);
    }
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart > 0)
        state->paint_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 /
                          (double)frequency.QuadPart;
    v1_update_message(state);
    EndPaint(hwnd, &paint);
}

static void v1_snapshot(cmp_v1_t *state, BOOL copy_only)
{
    RECT client;
    SYSTEMTIME now;
    cmp_snap_t snapshot;
    char lines[CMP_MAX_CELLS + 1][256] = { { 0 } };
    char text[256], path[MAX_PATH] = { 0 };
    int line_count = 0, height, i;
    BOOL copied;
    if (!state || !state->grid || !GetClientRect(state->grid, &client))
        return;
    GetLocalTime(&now);
    if (state->show_info) {
        _snprintf(text, sizeof(text),
                  "V1 | %d images | Lock %s | %04u-%02u-%02u %02u:%02u:%02u",
                  state->count, state->locked ? "on" : "off",
                  (unsigned int)now.wYear, (unsigned int)now.wMonth,
                  (unsigned int)now.wDay, (unsigned int)now.wHour,
                  (unsigned int)now.wMinute, (unsigned int)now.wSecond);
        CmpInfo_Add(lines, &line_count, text);
        for (i = 0; i < state->count; i++) {
            cmp_cell_t *cell = &state->cells[i];
            _snprintf(text, sizeof(text),
                      "%d: %s | %.0f%% | %dx%d | %dx%d", i + 1,
                      cell->image->name, cell->view.zoom * 100.0,
                      (int)(cell->image->img.w * cell->view.zoom),
                      (int)(cell->image->img.h * cell->view.zoom),
                      cell->image->img.w, cell->image->img.h);
            CmpInfo_Add(lines, &line_count, text);
        }
    }
    height = client.bottom + CmpInfo_Height(Cmp_UiFont(), line_count);
    if (height <= 0 ||
        !CmpSnap_Begin(state->hwnd, client.right, height, &snapshot)) {
        MessageBoxA(state->hwnd, "Could not allocate the snapshot image.",
                    "Snapshot", MB_OK | MB_ICONERROR);
        return;
    }
    v1_layout(state);
    v1_render(state, snapshot.dc, client.right, client.bottom,
              CMP_RENDER_SNAPSHOT);
    if (line_count)
        CmpInfo_Draw(snapshot.dc, client.right, client.bottom, Cmp_UiFont(),
                     lines, line_count);
    CmpSnap_Finalize(&snapshot);
    if (copy_only)
        copied = CmpSnap_CopyToClipboard(state->hwnd, snapshot.bitmap);
    else
        copied = CmpSnap_Deliver(state->hwnd, snapshot.bitmap, &now,
                                 state->cells[0].image->img.path,
                                 state->cells[0].image->img.path,
                                 state->count > 1 && state->cells[1].image ?
                                     state->cells[1].image->img.path :
                                     state->cells[0].image->img.path,
                                 path, sizeof(path));
    CmpSnap_End(&snapshot);
    if (!copied && (copy_only || !path[0])) {
        MessageBoxA(state->hwnd,
                    copy_only ? "Could not copy the snapshot to the clipboard." :
                                "Could not save or copy the snapshot.",
                    "Snapshot", MB_OK | MB_ICONERROR);
    } else if (!copy_only) {
        char message[MAX_PATH + 64];
        const char *filename = path[0] ? PathFindFileNameA(path) : NULL;
        if (filename && copied)
            _snprintf(message, sizeof(message), "Saved %s + clipboard",
                      filename);
        else if (filename)
            _snprintf(message, sizeof(message), "Saved %s", filename);
        else
            _snprintf(message, sizeof(message), "Copied to clipboard");
        message[sizeof(message) - 1] = '\0';
        SetWindowTextA(state->message, message);
    }
}

static int v1_hit(cmp_v1_t *state, POINT point, BOOL close_only)
{
    int i;
    for (i = 0; i < state->count; i++) {
        RECT *rect = close_only ? &state->cells[i].close_rect :
                                 &state->cells[i].image_rect;
        if (PtInRect(rect, point))
            return i;
    }
    return -1;
}

static void v1_clamp_all(cmp_v1_t *state)
{
    int i;
    for (i = 0; i < state->count; i++) {
        cmp_cell_t *cell = &state->cells[i];
        CmpView_ClampEdges(&cell->view, cell->image->img.w,
                           cell->image->img.h,
                           cell->image_rect.right - cell->image_rect.left,
                           cell->image_rect.bottom - cell->image_rect.top);
    }
}

static void v1_zoom_at(cmp_v1_t *state, int source, double zoom,
                       double x, double y)
{
    int i, first = state->locked ? 0 : source;
    int last = state->locked ? state->count : source + 1;
    for (i = first; i < last; i++) {
        cmp_cell_t *cell = &state->cells[i];
        CmpView_ZoomAt(&cell->view, zoom, x - cell->image_rect.left,
                       y - cell->image_rect.top,
                       cell->image_rect.right - cell->image_rect.left,
                       cell->image_rect.bottom - cell->image_rect.top);
    }
    v1_clamp_all(state);
    InvalidateRect(state->grid, NULL, FALSE);
}

static void v1_wheel_lock(cmp_v1_t *state, int source, POINT point,
                          int direction)
{
    cmp_cell_t *source_cell = &state->cells[source];
    double old_zoom = state->cells[0].view.zoom;
    double new_zoom = CmpZoom_Step(old_zoom, direction);
    double off_x = point.x - (source_cell->image_rect.left +
        (source_cell->image_rect.right - source_cell->image_rect.left) * 0.5);
    double off_y = point.y - (source_cell->image_rect.top +
        (source_cell->image_rect.bottom - source_cell->image_rect.top) * 0.5);
    double fx, fy;
    int i;
    if (fabs(new_zoom - old_zoom) < 1e-9)
        return;
    fx = (source_cell->view.u + off_x / source_cell->view.zoom) /
         source_cell->image->img.w;
    fy = (source_cell->view.v + off_y / source_cell->view.zoom) /
         source_cell->image->img.h;
    for (i = 0; i < state->count; i++) {
        cmp_cell_t *cell = &state->cells[i];
        int width = cell->image_rect.right - cell->image_rect.left;
        int height = cell->image_rect.bottom - cell->image_rect.top;
        cell->view.zoom = new_zoom;
        cell->view.u = fx * cell->image->img.w - off_x / new_zoom;
        cell->view.v = fy * cell->image->img.h - off_y / new_zoom;
        CmpView_ClampEdges(&cell->view, cell->image->img.w,
                           cell->image->img.h, width, height);
    }
    InvalidateRect(state->grid, NULL, FALSE);
}

static BOOL v1_supported_path(const wchar_t *path)
{
    const wchar_t *extension = PathFindExtensionW(path);
    return _wcsicmp(extension, L".png") == 0 ||
           _wcsicmp(extension, L".jpg") == 0 ||
           _wcsicmp(extension, L".jpeg") == 0 ||
           _wcsicmp(extension, L".bmp") == 0;
}

static void v1_add_dropped(cmp_v1_t *state, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xffffffffu, NULL, 0);
    UINT index;
    int added = 0, skipped = 0, limit_reached = 0;
    HCURSOR old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    for (index = 0; index < count; index++) {
        wchar_t wide[MAX_PATH];
        char path[MAX_PATH];
        UINT length = DragQueryFileW(drop, index, wide, MAX_PATH);
        int i;
        cmp_image_t *image;
        if (length == 0 || length >= MAX_PATH ||
            !wide_to_acp_strict(wide, path, sizeof(path)) ||
            !v1_supported_path(wide)) {
            skipped++;
            continue;
        }
        if (state->count >= CMP_MAX_CELLS) {
            limit_reached = 1;
            break;
        }
        for (i = 0; i < state->count; i++)
            if (lstrcmpiA(state->cells[i].image->img.path, path) == 0)
                break;
        if (i != state->count)
            continue;
        if (!Compare_CanOpen(1)) {
            limit_reached = 1;
            break;
        }
        {
            char loading[96];
            _snprintf(loading, sizeof(loading), "Loading %u/%u...",
                      index + 1, count);
            loading[sizeof(loading) - 1] = '\0';
            SetWindowTextA(state->message, loading);
            UpdateWindow(state->message);
        }
        image = CmpImage_Load(path);
        if (!image) {
            skipped++;
            continue;
        }
        state->cells[state->count++].image = image;
        added++;
    }
    SetCursor(old_cursor);
    DragFinish(drop);
    if (added) {
        state->need_fit = TRUE;
        v1_layout(state);
        if (state->locked)
            v1_fit_all(state);
        if (state->count >= 2)
            ShowWindow(state->hwnd, SW_SHOW);
        InvalidateRect(state->grid, NULL, FALSE);
    }
    if (skipped)
        MessageBoxA(state->hwnd, "Some files were skipped (unsupported, non-ACP, or unreadable).",
                    "Compare Files", MB_OK | MB_ICONINFORMATION);
    if (limit_reached)
        MessageBoxA(state->hwnd, "The comparison image limit is 8, and V1 supports at most four images.",
                    "Compare Files", MB_OK | MB_ICONINFORMATION);
}

static void v1_remove_cell(cmp_v1_t *state, int index)
{
    int i;
    if (index < 0 || index >= state->count)
        return;
    CmpImage_Unref(state->cells[index].image);
    for (i = index; i + 1 < state->count; i++)
        state->cells[i] = state->cells[i + 1];
    state->count--;
    ZeroMemory(&state->cells[state->count], sizeof(state->cells[0]));
    if (state->count < 2) {
        DestroyWindow(state->hwnd);
        return;
    }
    v1_layout(state);
    v1_fit_all(state);
}

static void v1_open_v2(cmp_v1_t *state)
{
    if (state->count >= 2)
        CompareV2_Open(state->cells[0].image, state->cells[1].image);
    SetFocus(state->grid);
}

static BOOL v1_create_tooltip(cmp_v1_t *state)
{
    TOOLINFOA info;
    state->tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, state->hwnd, NULL,
        GetModuleHandleA(NULL), NULL);
    if (!state->tooltip)
        return FALSE;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = state->hwnd;
    info.uId = (UINT_PTR)state->snapshot;
    info.lpszText = "Save snapshot as PNG and copy it to the clipboard.";
    if (!SendMessageA(state->tooltip, TTM_ADDTOOLA, 0, (LPARAM)&info)) {
        DestroyWindow(state->tooltip);
        state->tooltip = NULL;
        return FALSE;
    }
    return TRUE;
}

static LRESULT CALLBACK V1GridProc(HWND hwnd, UINT message, WPARAM wparam,
                                   LPARAM lparam)
{
    cmp_v1_t *state = v1_state(hwnd);
    POINT point;
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTA *)lparam)->lpCreateParams);
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state)
            v1_paint(state, hwnd);
        else {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_SIZE:
        if (state) {
            v1_layout(state);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (!state)
            break;
        SetFocus(hwnd);
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
        state->close_down = v1_hit(state, point, TRUE);
        if (state->close_down >= 0) {
            SetCapture(hwnd);
        } else {
            state->pan_source = v1_hit(state, point, FALSE);
            if (state->pan_source >= 0) {
                state->last = point;
                SetCapture(hwnd);
            }
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (state) {
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            state->pan_source = v1_hit(state, point, FALSE);
            state->close_down = -1;
            if (state->pan_source >= 0) {
                state->last = point;
                SetCapture(hwnd);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state) {
            int old_hover = state->hover;
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            state->hover = v1_hit(state, point, FALSE);
            if (state->pan_source >= 0 &&
                (wparam & (MK_LBUTTON | MK_RBUTTON))) {
                int dx = point.x - state->last.x;
                int dy = point.y - state->last.y;
                int first = state->locked ? 0 : state->pan_source;
                int last = state->locked ? state->count : state->pan_source + 1;
                int i;
                for (i = first; i < last; i++)
                    CmpView_Pan(&state->cells[i].view, dx, dy);
                v1_clamp_all(state);
                state->last = point;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (old_hover != state->hover) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state) {
            int remove_index = -1;
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            if (state->close_down >= 0 &&
                state->close_down == v1_hit(state, point, TRUE))
                remove_index = state->close_down;
            state->close_down = -1;
            state->pan_source = -1;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            if (remove_index >= 0) {
                v1_remove_cell(state, remove_index);
                return 0;
            }
        }
        return 0;
    case WM_RBUTTONUP:
        if (state) {
            state->pan_source = -1;
            if (GetCapture() == hwnd)
                ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state) {
            state->pan_source = -1;
            state->close_down = -1;
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (state) {
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            state->hover = v1_hit(state, point, FALSE);
            if (state->locked)
                v1_fit_all(state);
            else if (state->hover >= 0) {
                cmp_cell_t *cell = &state->cells[state->hover];
                CmpView_Fit(&cell->view, cell->image->img.w,
                            cell->image->img.h,
                            cell->image_rect.right - cell->image_rect.left,
                            cell->image_rect.bottom -                                 cell->image_rect.top,
                            1.0);
        if (state->locked)
            v1_fit_all(state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state) {
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            ScreenToClient(hwnd, &point);
            state->wheel_acc += GET_WHEEL_DELTA_WPARAM(wparam);
            while (state->wheel_acc >= WHEEL_DELTA ||
                   state->wheel_acc <= -WHEEL_DELTA) {
                int source = v1_hit(state, point, FALSE);
                int direction = state->wheel_acc > 0 ? 1 : -1;
                state->wheel_acc += direction > 0 ? -WHEEL_DELTA : WHEEL_DELTA;
                if (source >= 0) {
                    if (state->locked)
                        v1_wheel_lock(state, source, point, direction);
                    else {
                        cmp_view_t *view = &state->cells[source].view;
                        double next = CmpZoom_Step(view->zoom, direction);
                        v1_zoom_at(state, source, next, point.x, point.y);
                    }
                }
            }
        }
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK V1WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam)
{
    cmp_v1_t *state = v1_state(hwnd);
    switch (message) {
    case WM_NCCREATE:
        state = (cmp_v1_t *)((CREATESTRUCTA *)lparam)->lpCreateParams;
        state->hwnd = hwnd;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    case WM_CREATE: {
        HINSTANCE instance = ((CREATESTRUCTA *)lparam)->hInstance;
        state->lock = CreateWindowExA(0, "BUTTON", "Lock",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 7, 7, 67, 24, hwnd,
            (HMENU)V1_ID_LOCK, instance, NULL);
        state->v2 = CreateWindowExA(0, "BUTTON", "V2",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 77, 7, 45, 24, hwnd,
            (HMENU)V1_ID_V2, instance, NULL);
        state->snapshot = CreateWindowExA(0, "BUTTON", "Snapshot",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 126, 7, 82, 24, hwnd,
            (HMENU)V1_ID_SNAPSHOT, instance, NULL);
        state->info_bar = CreateWindowExA(0, "BUTTON", "Info bar",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 214, 7, 86, 24, hwnd,
            (HMENU)V1_ID_INFO, instance, NULL);
        state->message = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 306, 9, 560, 22, hwnd,
            NULL, instance, NULL);
        state->grid = CreateWindowExA(0, V1_GRID_CLASS, "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, V1_TOOLBAR_H, 0, 0,
            hwnd, NULL, instance, state);
        if (!state->lock || !state->v2 || !state->snapshot ||
            !state->info_bar || !state->message || !state->grid)
            return -1;
        if (!v1_create_tooltip(state))
            return -1;
        DragAcceptFiles(hwnd, TRUE);
        SendMessageA(state->lock, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->v2, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->snapshot, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->info_bar, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->message, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->info_bar, BM_SETCHECK, BST_CHECKED, 0);
        state->show_info = TRUE;
        v1_layout(state);
        return 0;
    }
    case WM_SIZE:
        if (state) {
            int width = LOWORD(lparam);
            int height = HIWORD(lparam) - V1_TOOLBAR_H;
            if (height < 0)
                height = 0;
            if (state->grid)
                SetWindowPos(state->grid, NULL, 0, V1_TOOLBAR_H,
                             width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_COMMAND:
        if (!state)
            break;
        if (LOWORD(wparam) == V1_ID_LOCK && HIWORD(wparam) == BN_CLICKED) {
            BOOL was_locked = state->locked;
            state->locked = SendMessageA(state->lock, BM_GETCHECK, 0, 0) ==
                            BST_CHECKED;
            if (!was_locked && state->locked)
                v1_fit_all(state);
            v1_update_message(state);
            InvalidateRect(state->grid, NULL, FALSE);
            SetFocus(state->grid);
            return 0;
        }
        if (LOWORD(wparam) == V1_ID_V2 && HIWORD(wparam) == BN_CLICKED) {
            v1_open_v2(state);
            return 0;
        }
        if (LOWORD(wparam) == V1_ID_SNAPSHOT && HIWORD(wparam) == BN_CLICKED) {
            v1_snapshot(state, FALSE);
            SetFocus(state->grid);
            return 0;
        }
        if (LOWORD(wparam) == V1_ID_INFO && HIWORD(wparam) == BN_CLICKED) {
            state->show_info =
                SendMessageA(state->info_bar, BM_GETCHECK, 0, 0) ==
                BST_CHECKED;
            SetFocus(state->grid);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (state && state->grid)
            SendMessageA(state->grid, message, wparam, lparam);
        return 0;
    case WM_DROPFILES:
        if (state)
            v1_add_dropped(state, (HDROP)wparam);
        else
            DragFinish((HDROP)wparam);
        return 0;
    case CMPM_KEY:
        if (!state)
            return 0;
        {
        BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wparam == VK_ESCAPE ||
            (wparam == 'W' && ctrl)) {
            DestroyWindow(hwnd);
            return 1;
        }
        if (wparam == 'S' && ctrl && shift) {
            v1_snapshot(state, FALSE);
            return 1;
        }
        if (wparam == 'S' && ctrl) {
            v1_snapshot(state, FALSE);
            return 1;
        }
        if (wparam == 'C' && ctrl) {
            v1_snapshot(state, TRUE);
            return 1;
        }
        if (!ctrl && wparam == 'L') {
            SendMessageA(state->lock, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == 'V') {
            v1_open_v2(state);
            return 1;
        }
        if (!ctrl && wparam == '0') {
            int index = state->locked ? -1 :
                        (state->hover >= 0 ? state->hover : 0);
            if (index < 0)
                v1_fit_all(state);
            else {
                cmp_cell_t *cell = &state->cells[index];
                CmpView_Fit(&cell->view, cell->image->img.w,
                            cell->image->img.h,
                            cell->image_rect.right - cell->image_rect.left,
                            cell->image_rect.bottom - cell->image_rect.top,
                            1.0);
                InvalidateRect(state->grid, NULL, FALSE);
            }
            return 1;
        }
        if (wparam == VK_ADD || wparam == VK_OEM_PLUS ||
            wparam == VK_SUBTRACT || wparam == VK_OEM_MINUS) {
            if (ctrl)
                return 0;
            int index = state->hover >= 0 ? state->hover : 0;
            double zoom = CmpZoom_Step(state->cells[index].view.zoom,
                (wparam == VK_ADD || wparam == VK_OEM_PLUS) ? 1 : -1);
            cmp_cell_t *cell = &state->cells[index];
            v1_zoom_at(state, index, zoom,
                       cell->image_rect.left +
                           (cell->image_rect.right - cell->image_rect.left) * 0.5,
                       cell->image_rect.top +
                           (cell->image_rect.bottom - cell->image_rect.top) * 0.5);
            return 1;
        }
        return 0;
        }
    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
        if (state && state->tooltip) {
            DestroyWindow(state->tooltip);
            state->tooltip = NULL;
        }
        if (state)
            v1_release(state);
        return 0;
    case WM_NCDESTROY:
        if (state) {
            v1_release(state);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            free(state);
        }
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

BOOL Compare_Init(HINSTANCE instance, HFONT font)
{
    WNDCLASSEXA wc;
    Compare_SetFont(font);
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = V1WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = V1_CLASS;
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    wc.lpfnWndProc = V1GridProc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DBLCLKS;
    wc.lpszClassName = V1_GRID_CLASS;
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    return CompareV2_Register(instance);
}

HWND CompareV1_Open(cmp_image_t **images, int count)
{
    cmp_v1_t *state;
    HWND hwnd;
    POINT cursor;
    HMONITOR monitor;
    MONITORINFO info;
    int width, height, x, y, i;
    if (!images || count < 2 || count > CMP_MAX_CELLS)
        return NULL;
    for (i = 0; i < count; i++)
        if (!images[i] || !images[i]->img.valid)
            return NULL;
    state = (cmp_v1_t *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->count = count;
    state->pan_source = -1;
    state->close_down = -1;
    state->hover = -1;
    state->need_fit = TRUE;
    for (i = 0; i < count; i++)
        state->cells[i].image = CmpImage_Ref(images[i]);
    width = 900;
    height = 650;
    if (!GetCursorPos(&cursor))
        cursor.x = cursor.y = 0;
    monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetMonitorInfoA(monitor, &info)) {
        width = (info.rcWork.right - info.rcWork.left) * 4 / 5;
        height = (info.rcWork.bottom - info.rcWork.top) * 4 / 5;
        x = info.rcWork.left +
            ((info.rcWork.right - info.rcWork.left) - width) / 2;
        y = info.rcWork.top +
            ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    } else {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    }
    hwnd = CreateWindowExA(WS_EX_ACCEPTFILES, V1_CLASS, "Compare Files",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           x, y, width, height, NULL, NULL,
                           GetModuleHandleA(NULL), state);
    if (!hwnd)
        return NULL;
    if (!CmpReg_Add(hwnd)) {
        DestroyWindow(hwnd);
        return NULL;
    }
    state->registered = TRUE;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}
