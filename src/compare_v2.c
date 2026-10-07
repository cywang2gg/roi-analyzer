#include "compare.h"

#include <commctrl.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metrics_async.h"
#include "ui_scale.h"

#define V2_TOP_H       76
#define V2_GROUP_H     68
#define V2_BOTTOM_H     28
#define V2_ID_TRACK_L  4201
#define V2_ID_TRACK_R  4202
#define V2_ID_SYNC     4203
#define V2_ID_LEFT     4204
#define V2_ID_RIGHT    4205
#define V2_ID_SWAP     4206
#define V2_ID_SPLIT    4207
#define V2_ID_RESET    4208
#define V2_ID_SNAPSHOT 4209
#define V2_ID_INFO     4210
#define V2_ID_METRICS  CMP_ID_METRICS

enum { V2_DRAG_NONE = 0, V2_DRAG_SPLIT, V2_DRAG_PAN };

typedef struct {
    HWND hwnd;
    HWND overlay;
    HWND status;
    HWND track[2];
    HWND label[2];
    RECT grp_rc[4];
    HWND btn_ana[3];
    HWND sync;
    HWND pan_left;
    HWND pan_right;
    HWND swap_button;
    HWND split_button;
    HWND reset_button;
    HWND snapshot_button;
    HWND metrics_button;
    HWND info_bar;
    HWND progress;
    HWND tooltip;
    cmp_image_t *image[2];
    cmp_image_t *ana[2];
    cmp_metric_t met[2];
    RECT met_src[2];
    cmp_ana_t mode;
    int best;
    metrics_async_job_t *metrics_job;
    cmp_view_t view[2];
    BOOL swapped;
    BOOL split_mode;
    BOOL sync_pan;
    int pan_side;
    double split_fraction;
    BOOL need_fit;
    BOOL show_info;
    int drag;
    POINT last;
    int wheel_acc;
    BOOL have_pointer;
    POINT pointer;
    HDC mem_dc;
    HBITMAP mem_bitmap;
    HBITMAP old_bitmap;
    int mem_w;
    int mem_h;
    double paint_ms;
    BOOL registered;
} cmp_v2_t;

static const char V2_CLASS[] = "RoiCmpV2";
static const char V2_OVERLAY_CLASS[] = "RoiCmpOverlay";

static void v2_snapshot(cmp_v2_t *state, BOOL copy_only);
static BOOL v2_run_metrics(cmp_v2_t *state);
static void v2_metrics_update(cmp_v2_t *state);
static void v2_metrics_schedule(cmp_v2_t *state);
static BOOL v2_set_mode(cmp_v2_t *state, cmp_ana_t mode);
static void v2_update_status(cmp_v2_t *state);

static cmp_image_t *v2_disp(const cmp_v2_t *state, int index)
{
    return (state->mode != CMP_ANA_NONE && state->ana[index]) ?
           state->ana[index] : state->image[index];
}

static void v2_sync_ana_buttons(cmp_v2_t *state)
{
    int k;
    for (k = 0; k < 3; k++)
        SendMessageA(state->btn_ana[k], BM_SETCHECK,
                     (state->mode == (cmp_ana_t)(CMP_ANA_SHARP + k)) ?
                     BST_CHECKED : BST_UNCHECKED, 0);
}

static void v2_metrics_update(cmp_v2_t *state)
{
    RECT full;
    int k;
    ZeroMemory(&full, sizeof(full));
    if (!state->overlay || !GetClientRect(state->overlay, &full))
        return;
    for (k = 0; k < 2; k++) {
        RECT src;
        if (state->mode == CMP_ANA_NONE ||
            !CmpAna_VisibleSrcImage(state->image[k], &state->view[k],
                                    full.right, full.bottom, &src)) {
            ZeroMemory(&state->met[k], sizeof(state->met[k]));
            SetRectEmpty(&state->met_src[k]);
        } else if (state->met[k].mode != state->mode ||
                   !EqualRect(&src, &state->met_src[k])) {
            if (!CmpAna_Measure(state->image[k], state->mode, &src,
                                &state->met[k]))
                OutputDebugStringA("ROI Analyzer: comparison metric measurement failed.\n");
            state->met_src[k] = src;
        }
    }
    state->best = CmpAna_BestIndex(state->met, 2);
}

static void v2_metrics_schedule(cmp_v2_t *state)
{
    if (state->mode != CMP_ANA_NONE &&
        !SetTimer(state->hwnd, CMP_TID_METRIC, CMP_ANA_DEBOUNCE_MS, NULL))
        OutputDebugStringA("ROI Analyzer: could not schedule comparison metrics.\n");
}

static BOOL v2_set_mode(cmp_v2_t *state, cmp_ana_t mode)
{
    cmp_image_t *acquired[2] = { NULL, NULL };
    HCURSOR previous_cursor = NULL;
    BOOL wait_cursor = FALSE;
    int k;
    if (!state || mode < CMP_ANA_NONE || mode >= CMP_ANA_COUNT)
        return FALSE;
    if (state->mode == mode)
        mode = CMP_ANA_NONE;
    if (mode != CMP_ANA_NONE) {
        for (k = 0; k < 2; k++)
            if (!state->image[k]->ana[mode] &&
                !state->image[k]->ana_failed[mode])
                wait_cursor = TRUE;
        if (wait_cursor)
            previous_cursor = SetCursor(LoadCursorA(NULL, IDC_WAIT));
        for (k = 0; k < 2; k++) {
            char progress[64];
            if (wait_cursor) {
                _snprintf(progress, sizeof(progress), "Analyzing %d/2...",
                          k + 1);
                progress[sizeof(progress) - 1] = '\0';
                SetWindowTextA(state->status, progress);
                UpdateWindow(state->status);
            }
            acquired[k] = CmpAna_Acquire(state->image[k], mode);
            if (!acquired[k]) {
                int j;
                for (j = 0; j < k; j++)
                    CmpAna_Release(state->image[j], mode);
                if (wait_cursor)
                    SetCursor(previous_cursor);
                v2_update_status(state);
                v2_sync_ana_buttons(state);
                MessageBoxA(state->hwnd,
                            "Analysis could not be allocated. The current mode was kept.",
                            "Compare Analysis", MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
    }
    KillTimer(state->hwnd, CMP_TID_METRIC);
    for (k = 0; k < 2; k++) {
        if (state->mode != CMP_ANA_NONE)
            CmpAna_Release(state->image[k], state->mode);
        state->ana[k] = acquired[k];
        ZeroMemory(&state->met[k], sizeof(state->met[k]));
        SetRectEmpty(&state->met_src[k]);
    }
    state->mode = mode;
    v2_sync_ana_buttons(state);
    v2_metrics_update(state);
    if (wait_cursor)
        SetCursor(previous_cursor);
    v2_update_status(state);
    InvalidateRect(state->overlay, NULL, FALSE);
    return TRUE;
}

static double v2_clamp(double value, double minimum, double maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static cmp_v2_t *v2_state(HWND hwnd)
{
    return (cmp_v2_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
}

static int v2_image_index(const cmp_v2_t *state, int side)
{
    return side ^ (state->swapped ? 1 : 0);
}

static void v2_free_backbuffer(cmp_v2_t *state)
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

static void v2_release(cmp_v2_t *state)
{
    if (!state)
        return;
    KillTimer(state->hwnd, CMP_TID_METRIC);
    if (state->metrics_job) {
        Metrics_CancelAsync(state->metrics_job);
        Metrics_ReleaseAsync(state->metrics_job);
        state->metrics_job = NULL;
    }
    Metrics_CloseProgress(state->progress);
    state->progress = NULL;
    CmpReg_Remove(state->hwnd);
    state->registered = FALSE;
    v2_free_backbuffer(state);
    if (state->ana[0])
        CmpAna_Release(state->image[0], state->mode);
    if (state->ana[1])
        CmpAna_Release(state->image[1], state->mode);
    state->ana[0] = state->ana[1] = NULL;
    CmpImage_Unref(state->image[0]);
    CmpImage_Unref(state->image[1]);
    state->image[0] = state->image[1] = NULL;
}

static BOOL v2_visible_roi(cmp_v2_t *state, int side, RECT *roi)
{
    RECT client, visible, clip;
    double x0, y0, x1, y1;
    int index = v2_image_index(state, side);
    int split;
    cmp_image_t *image = state->image[index];
    if (!GetClientRect(state->overlay, &client) ||
        !CmpView_VisibleRect(&state->view[index], client.right,
                             client.bottom, image->img.w, image->img.h,
                             &visible))
        return FALSE;
    split = (int)floor(state->split_fraction * client.right + 0.5);
    if (split < 0) split = 0;
    if (split > client.right) split = client.right;
    clip.left = side == 0 ? 0 : split;
    clip.right = side == 0 ? split : client.right;
    clip.top = 0;
    clip.bottom = client.bottom;
    if (visible.left < clip.left) visible.left = clip.left;
    if (visible.right > clip.right) visible.right = clip.right;
    if (visible.right <= visible.left || visible.bottom <= visible.top)
        return FALSE;
    CmpView_ScreenToImage(&state->view[index], client.right, client.bottom,
                          visible.left, visible.top, &x0, &y0);
    CmpView_ScreenToImage(&state->view[index], client.right, client.bottom,
                          visible.right, visible.bottom, &x1, &y1);
    roi->left = (LONG)floor(x0);
    roi->top = (LONG)floor(y0);
    roi->right = (LONG)ceil(x1);
    roi->bottom = (LONG)ceil(y1);
    if (roi->left < 0) roi->left = 0;
    if (roi->top < 0) roi->top = 0;
    if (roi->right > image->img.w) roi->right = image->img.w;
    if (roi->bottom > image->img.h) roi->bottom = image->img.h;
    return roi->right > roi->left && roi->bottom > roi->top;
}

static BOOL v2_run_metrics(cmp_v2_t *state)
{
    cmp_image_t *images[2];
    RECT rois[2];
    metrics_async_job_t *job = NULL;
    int side;
    if (!state || state->metrics_job)
        return FALSE;
    for (side = 0; side < 2; side++) {
        int index = v2_image_index(state, side);
        images[side] = state->image[index];
        if (!v2_visible_roi(state, side, &rois[side])) {
            MessageBoxA(state->hwnd, "No visible image region is available.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }
    if (!Compare_RunMetrics(state->hwnd, images, rois, 2, &job))
        return FALSE;
    if (job) {
        state->metrics_job = job;
        EnableWindow(state->metrics_button, FALSE);
        SetWindowTextA(state->metrics_button, "Analyzing...");
        state->progress = Metrics_ShowProgress(state->hwnd);
        if (!state->progress)
            OutputDebugStringA("ROI Analyzer: could not create metrics progress dialog.\n");
    }
    return TRUE;
}

static void v2_sync_controls(cmp_v2_t *state)
{
    int side;
    for (side = 0; side < 2; side++) {
        int index = v2_image_index(state, side);
        int position = (int)floor(state->view[index].zoom * 100.0 + 0.5);
        char label[24];
        SendMessageA(state->track[side], TBM_SETPOS, TRUE, position);
        _snprintf(label, sizeof(label), "%c: %d%%", side == 0 ? 'L' : 'R',
                  position);
        label[sizeof(label) - 1] = '\0';
        SetWindowTextA(state->label[side], label);
    }
}

static void v2_update_status(cmp_v2_t *state)
{
    char text[512];
    int left_index = v2_image_index(state, 0);
    int right_index = v2_image_index(state, 1);
    int width, height, split;
    if (!state->status || !state->overlay)
        return;
    {
        RECT client;
        GetClientRect(state->overlay, &client);
        width = client.right;
        height = client.bottom;
    }
    split = (int)floor(state->split_fraction * width + 0.5);
    if (state->have_pointer && width > 0 && height > 0) {
        int i;
        int coords[4];
        int rgb[6];
        BOOL valid[2] = { FALSE, FALSE };
        for (i = 0; i < 2; i++) {
            cmp_image_t *image = state->image[v2_image_index(state, i)];
            double x, y;
            int ix, iy;
            CmpView_ScreenToImage(&state->view[v2_image_index(state, i)],
                                  width, height, state->pointer.x,
                                  state->pointer.y, &x, &y);
            ix = (int)floor(x);
            iy = (int)floor(y);
            if (ix >= 0 && iy >= 0 && ix < image->img.w &&
                iy < image->img.h) {
                const BYTE *pixel = image->img.px +
                    (size_t)iy * (size_t)image->img.pitch + (size_t)ix * 4;
                coords[i * 2] = ix;
                coords[i * 2 + 1] = iy;
                rgb[i * 3] = pixel[2];
                rgb[i * 3 + 1] = pixel[1];
                rgb[i * 3 + 2] = pixel[0];
                valid[i] = TRUE;
            }
        }
        _snprintf(text, sizeof(text),
                  "L %d%% | R %d%% | split %d%% | pan %s(%c) | sync %s | "
                  "L: %s | R: %s",
                  (int)(state->view[left_index].zoom * 100.0 + 0.5),
                  (int)(state->view[right_index].zoom * 100.0 + 0.5),
                  width > 0 ? split * 100 / width : 50,
                  state->pan_side == 0 ? "Left" : "Right",
                  v2_image_index(state, state->pan_side) == 0 ? 'A' : 'B',
                  state->sync_pan ? "on" : "off",
                  valid[0] ? "pixel" : "--",
                  valid[1] ? "pixel" : "--");
        if (valid[0] || valid[1]) {
            char values[180];
            _snprintf(values, sizeof(values),
                      " | L (%d,%d) RGB(%d,%d,%d) | R (%d,%d) RGB(%d,%d,%d)",
                      valid[0] ? coords[0] : -1, valid[0] ? coords[1] : -1,
                      valid[0] ? rgb[0] : -1, valid[0] ? rgb[1] : -1,
                      valid[0] ? rgb[2] : -1, valid[1] ? coords[2] : -1,
                      valid[1] ? coords[3] : -1, valid[1] ? rgb[3] : -1,
                      valid[1] ? rgb[4] : -1, valid[1] ? rgb[5] : -1);
            values[sizeof(values) - 1] = '\0';
            strncat(text, values, sizeof(text) - strlen(text) - 1);
        }
    } else {
        _snprintf(text, sizeof(text),
                  "L %d%% | R %d%% | split %d%% | pan %s(%c) | sync %s",
                  (int)(state->view[left_index].zoom * 100.0 + 0.5),
                  (int)(state->view[right_index].zoom * 100.0 + 0.5),
                  width > 0 ? split * 100 / width : 50,
                  state->pan_side == 0 ? "Left" : "Right",
                  v2_image_index(state, state->pan_side) == 0 ? 'A' : 'B',
                  state->sync_pan ? "on" : "off");
    }
    text[sizeof(text) - 1] = '\0';
    SetWindowTextA(state->status, text);
}

static void v2_update_title(cmp_v2_t *state)
{
    char title[MAX_PATH * 2 + 40];
    _snprintf(title, sizeof(title), "Compare - L: %s | R: %s",
              state->image[v2_image_index(state, 0)]->name,
              state->image[v2_image_index(state, 1)]->name);
    title[sizeof(title) - 1] = '\0';
    SetWindowTextA(state->hwnd, title);
}

static void v2_fit_views(cmp_v2_t *state)
{
    RECT client;
    int i;
    if (!state->overlay || !GetClientRect(state->overlay, &client) ||
        client.right <= 0 || client.bottom <= 0) {
        state->need_fit = TRUE;
        return;
    }
    for (i = 0; i < 2; i++)
        CmpView_Fit(&state->view[i], state->image[i]->img.w,
                    state->image[i]->img.h, client.right, client.bottom, 0.95);
    state->need_fit = FALSE;
    v2_sync_controls(state);
    v2_metrics_schedule(state);
}

static void v2_reset_all(cmp_v2_t *state)
{
    state->swapped = FALSE;
    state->split_fraction = 0.5;
    state->split_mode = TRUE;
    SendMessageA(state->split_button, BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextA(state->split_button, "Split: ON");
    v2_fit_views(state);
    v2_update_title(state);
    v2_update_status(state);
    InvalidateRect(state->overlay, NULL, FALSE);
}

static BOOL v2_create_tooltip(cmp_v2_t *state)
{
    TOOLINFOA info;
    int i;
    const char *texts[3] = {
        "Show the Sharp Sobel edge analysis.",
        "Show the Texture high-pass analysis.",
        "Show the Neutral Lab color analysis."
    };
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
    info.uId = (UINT_PTR)state->snapshot_button;
    info.lpszText = "Save snapshot as PNG and copy it to the clipboard.";
    if (!SendMessageA(state->tooltip, TTM_ADDTOOLA, 0, (LPARAM)&info)) {
        DestroyWindow(state->tooltip);
        state->tooltip = NULL;
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = state->hwnd;
        info.uId = (UINT_PTR)state->btn_ana[i];
        info.lpszText = (LPSTR)texts[i];
        if (!SendMessageA(state->tooltip, TTM_ADDTOOLA, 0,
                          (LPARAM)&info)) {
            DestroyWindow(state->tooltip);
            state->tooltip = NULL;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL v2_prepare_backbuffer(cmp_v2_t *state, HDC dc,
                                  int width, int height)
{
    if (width <= 0 || height <= 0)
        return FALSE;
    if (state->mem_dc && state->mem_w == width && state->mem_h == height)
        return TRUE;
    v2_free_backbuffer(state);
    state->mem_dc = CreateCompatibleDC(dc);
    state->mem_bitmap = CreateCompatibleBitmap(dc, width, height);
    if (!state->mem_dc || !state->mem_bitmap) {
        v2_free_backbuffer(state);
        return FALSE;
    }
    state->old_bitmap = (HBITMAP)SelectObject(state->mem_dc,
                                              state->mem_bitmap);
    state->mem_w = width;
    state->mem_h = height;
    return TRUE;
}

static RECT v2_scale_rect(const RECT *rect, double scale)
{
    RECT scaled;
    scaled.left = Snap_Round(rect->left * scale);
    scaled.top = Snap_Round(rect->top * scale);
    scaled.right = Snap_Round(rect->right * scale);
    scaled.bottom = Snap_Round(rect->bottom * scale);
    return scaled;
}

static void v2_render(cmp_v2_t *state, HDC dc, int width, int height,
                      unsigned int flags, double scale, HFONT font)
{
    RECT client, viewport, left_clip, right_clip;
    HGDIOBJ old_font;
    int split, left_index, right_index, line_width;
    client.left = 0;
    client.top = 0;
    client.right = Snap_Round(width * scale);
    client.bottom = Snap_Round(height * scale);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    viewport = client;
    split = (int)floor(state->split_fraction * client.right + 0.5);
    if (split < 0) split = 0;
    if (split > client.right) split = client.right;
    left_clip.left = 0;
    left_clip.top = 0;
    left_clip.right = split;
    left_clip.bottom = client.bottom;
    right_clip.left = split;
    right_clip.top = 0;
    right_clip.right = client.right;
    right_clip.bottom = client.bottom;
    left_index = v2_image_index(state, 0);
    right_index = v2_image_index(state, 1);
    {
        cmp_view_t left_view = state->view[left_index];
        cmp_view_t right_view = state->view[right_index];
        left_view.zoom *= scale;
        right_view.zoom *= scale;
        Cmp_Blit(dc, v2_disp(state, left_index), &left_view, &viewport,
                 &left_clip);
        Cmp_Blit(dc, v2_disp(state, right_index), &right_view, &viewport,
                 &right_clip);
    }
    if (client.right > 0) {
        HBRUSH yellow = CreateSolidBrush(RGB(255, 220, 0));
        HGDIOBJ old_brush = SelectObject(dc, yellow);
        int line_left, line_right;
        line_width = (flags & CMP_RENDER_SNAPSHOT) ?
                     Snap_Round(scale) : Ui_Scale(2);
        if (line_width < 1)
            line_width = 1;
        line_left = split - line_width / 2;
        line_right = line_left + line_width;
        if (line_left < 0) {
            line_left = 0;
            line_right = line_width < client.right ?
                         line_width : client.right;
        }
        if (line_right > client.right) {
            line_right = client.right;
            line_left = client.right > line_width ?
                        client.right - line_width : 0;
        }
        if (line_right > line_left)
            PatBlt(dc, line_left, 0, line_right - line_left,
                   client.bottom, PATCOPY);
        SelectObject(dc, old_brush);
        DeleteObject(yellow);
    }
    {
        char left_label[MAX_PATH + 180], right_label[MAX_PATH + 180];
        char left_metric[128], right_metric[128];
        RECT label;
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        old_font = SelectObject(dc, font ? font : Cmp_UiFont());
        left_metric[0] = '\0';
        right_metric[0] = '\0';
        if (state->mode != CMP_ANA_NONE) {
            CmpAna_Format(&state->met[left_index], left_metric,
                          sizeof(left_metric));
            CmpAna_Format(&state->met[right_index], right_metric,
                          sizeof(right_metric));
            _snprintf(left_label, sizeof(left_label), "A: %s | %s %.0f%%",
                      left_metric, state->image[left_index]->name,
                      state->view[left_index].zoom * 100.0);
            _snprintf(right_label, sizeof(right_label), "B: %s | %s %.0f%%",
                      right_metric, state->image[right_index]->name,
                      state->view[right_index].zoom * 100.0);
        } else {
            _snprintf(left_label, sizeof(left_label), "A: %s %.0f%%",
                      state->image[left_index]->name,
                      state->view[left_index].zoom * 100.0);
            _snprintf(right_label, sizeof(right_label), "B: %s %.0f%%",
                      state->image[right_index]->name,
                      state->view[right_index].zoom * 100.0);
        }
        left_label[sizeof(left_label) - 1] = '\0';
        right_label[sizeof(right_label) - 1] = '\0';
        if (flags & CMP_RENDER_SNAPSHOT) {
            RECT logical = { Ui_Scale(8), Ui_Scale(8), width / 2,
                             Ui_Scale(32) };
            label = v2_scale_rect(&logical, scale);
        } else {
            label.left = Ui_Scale(8);
            label.top = Ui_Scale(8);
            label.right = client.right / 2;
            label.bottom = Ui_Scale(32);
        }
        FillRect(dc, &label, black);
        SetTextColor(dc, state->mode != CMP_ANA_NONE &&
                     left_index == state->best ? RGB(255, 220, 0) :
                     RGB(255, 255, 255));
        DrawTextA(dc, left_label, -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
        if (flags & CMP_RENDER_SNAPSHOT) {
            RECT logical = { width / 2, Ui_Scale(8),
                             width - Ui_Scale(8), Ui_Scale(32) };
            label = v2_scale_rect(&logical, scale);
        } else {
            label.left = client.right / 2;
            label.right = client.right - Ui_Scale(8);
            label.top = Ui_Scale(8);
            label.bottom = Ui_Scale(32);
        }
        FillRect(dc, &label, black);
        SetTextColor(dc, state->mode != CMP_ANA_NONE &&
                     right_index == state->best ? RGB(255, 220, 0) :
                     RGB(255, 255, 255));
        DrawTextA(dc, right_label, -1, &label,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(dc, RGB(255, 255, 255));
        DeleteObject(black);
        if (old_font)
            SelectObject(dc, old_font);
    }
}

static void v2_paint(cmp_v2_t *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client;
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);
    GetClientRect(hwnd, &client);
    if (v2_prepare_backbuffer(state, dc, client.right, client.bottom)) {
        v2_render(state, state->mem_dc, client.right, client.bottom, 0, 1.0,
                  Cmp_UiFont());
        BitBlt(dc, 0, 0, client.right, client.bottom,
               state->mem_dc, 0, 0, SRCCOPY);
    }
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart > 0)
        state->paint_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 /
                          (double)frequency.QuadPart;
    EndPaint(hwnd, &paint);
}

static void v2_snapshot(cmp_v2_t *state, BOOL copy_only)
{
    RECT client;
    SYSTEMTIME now;
    cmp_snap_t snapshot;
    char lines[CMP_MAX_CELLS + 1][256] = { { 0 } };
    char text[256], path[MAX_PATH] = { 0 };
    double scale;
    HFONT font;
    int line_count = 0, height, output_width, output_height, i;
    BOOL copied;
    if (!state || !state->overlay ||
        !GetClientRect(state->overlay, &client))
        return;
    scale = Snap_PhysicalScale(state->overlay);
    output_width = Snap_Round(client.right * scale);
    output_height = Snap_Round(client.bottom * scale);
    font = CmpSnap_CreateScaledFont(scale);
    if (output_width <= 0 || output_height <= 0 || !font) {
        MessageBoxA(state->hwnd, "Could not prepare the snapshot rendering.",
                    "Snapshot", MB_OK | MB_ICONERROR);
        if (font)
            DeleteObject(font);
        return;
    }
    GetLocalTime(&now);
    if (state->show_info) {
        _snprintf(text, sizeof(text),
                  "V2 | split %d%% | sync %s | pan %s | %04u-%02u-%02u %02u:%02u:%02u",
                  (int)(state->split_fraction * 100.0 + 0.5),
                  state->sync_pan ? "on" : "off",
                  state->pan_side == 0 ? "A" : "B",
                  (unsigned int)now.wYear, (unsigned int)now.wMonth,
                  (unsigned int)now.wDay, (unsigned int)now.wHour,
                  (unsigned int)now.wMinute, (unsigned int)now.wSecond);
        CmpInfo_Add(lines, &line_count, text);
        for (i = 0; i < 2; i++) {
            int index = v2_image_index(state, i);
            cmp_image_t *image = state->image[index];
            RECT visible;
            int visible_w = 0, visible_h = 0;
            if (CmpView_VisibleRect(&state->view[index], client.right,
                                    client.bottom, image->img.w,
                                    image->img.h, &visible)) {
                visible_w = visible.right - visible.left;
                visible_h = visible.bottom - visible.top;
            }
            _snprintf(text, sizeof(text),
                      "%c: %s | %.0f%% | src %dx%d | %dx%d",
                      i == 0 ? 'A' : 'B', image->name,
                      state->view[index].zoom * 100.0,
                      visible_w, visible_h,
                      image->img.w, image->img.h);
            CmpInfo_Add(lines, &line_count, text);
        }
    }
    height = output_height + CmpInfo_Height(font, line_count);
    if (height <= 0 ||
        !CmpSnap_Begin(state->hwnd, output_width, height, &snapshot)) {
        MessageBoxA(state->hwnd, "Could not allocate the snapshot image.",
                    "Snapshot", MB_OK | MB_ICONERROR);
        DeleteObject(font);
        return;
    }
    v2_render(state, snapshot.dc, client.right, client.bottom,
              CMP_RENDER_SNAPSHOT, scale, font);
    if (line_count)
        CmpInfo_Draw(snapshot.dc, output_width, output_height, font,
                     lines, line_count);
    DeleteObject(font);
    CmpSnap_Finalize(&snapshot);
    if (copy_only)
        copied = CmpSnap_CopyToClipboard(state->hwnd, snapshot.bitmap);
    else
        copied = CmpSnap_Deliver(
            state->hwnd, snapshot.bitmap, &now,
            state->image[v2_image_index(state, 0)]->img.path,
            state->image[v2_image_index(state, 0)]->img.path,
            state->image[v2_image_index(state, 1)]->img.path,
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
        SetWindowTextA(state->status, message);
    }
}

static void v2_layout(cmp_v2_t *state)
{
    RECT client;
    int width, gz, zoom_w, gx1, pan_w, gx2, act_w, gx3, ana_w;
    int label_w, track_x, track_w, button_w, lower_w, ana_button_w, usable;
    if (!GetClientRect(state->hwnd, &client))
        return;
    width = client.right;
    gz = Ui_Scale(4);
    usable = width - Ui_Scale(20);
    if (usable < 0)
        usable = 0;
    zoom_w = usable * 2 / 5;
    gx1 = gz + zoom_w + Ui_Scale(4);
    pan_w = usable / 5;
    gx2 = gx1 + pan_w + Ui_Scale(4);
    act_w = usable / 5;
    gx3 = gx2 + act_w + Ui_Scale(4);
    ana_w = usable - zoom_w - pan_w - act_w;
    MoveWindow(state->overlay, 0, Ui_Scale(V2_TOP_H), width,
               client.bottom - Ui_Scale(V2_TOP_H) -
                   Ui_Scale(V2_BOTTOM_H), TRUE);
    MoveWindow(state->status, 0, client.bottom - Ui_Scale(V2_BOTTOM_H),
               width, Ui_Scale(V2_BOTTOM_H), TRUE);
    SetRect(&state->grp_rc[0], gz, Ui_Scale(2), gz + zoom_w,
            Ui_Scale(2) + Ui_Scale(V2_GROUP_H));
    SetRect(&state->grp_rc[1], gx1, Ui_Scale(2), gx1 + pan_w,
            Ui_Scale(2) + Ui_Scale(V2_GROUP_H));
    SetRect(&state->grp_rc[2], gx2, Ui_Scale(2), gx2 + act_w,
            Ui_Scale(2) + Ui_Scale(V2_GROUP_H));
    SetRect(&state->grp_rc[3], gx3, Ui_Scale(2), gx3 + ana_w,
            Ui_Scale(2) + Ui_Scale(V2_GROUP_H));
    label_w = Ui_Scale(64);
    track_x = gz + Ui_Scale(8) + label_w + Ui_Scale(6);
    track_w = zoom_w - (track_x - gz) - Ui_Scale(10);
    if (track_w < Ui_Scale(80))
        track_w = Ui_Scale(80);
    MoveWindow(state->label[0], gz + Ui_Scale(8), Ui_Scale(20),
               label_w, Ui_Scale(22), TRUE);
    MoveWindow(state->track[0], track_x, Ui_Scale(18), track_w,
               Ui_Scale(24), TRUE);
    MoveWindow(state->label[1], gz + Ui_Scale(8), Ui_Scale(46),
               label_w, Ui_Scale(22), TRUE);
    MoveWindow(state->track[1], track_x, Ui_Scale(44), track_w,
               Ui_Scale(24), TRUE);
    MoveWindow(state->sync, gx1 + Ui_Scale(8), Ui_Scale(20),
               Ui_Scale(130), Ui_Scale(22), TRUE);
    MoveWindow(state->pan_left, gx1 + Ui_Scale(8), Ui_Scale(42),
               Ui_Scale(70), Ui_Scale(22), TRUE);
    MoveWindow(state->pan_right, gx1 + Ui_Scale(82), Ui_Scale(42),
               Ui_Scale(78), Ui_Scale(22), TRUE);
    button_w = (act_w - Ui_Scale(24)) / 3;
    if (button_w < 1)
        button_w = 1;
    MoveWindow(state->swap_button, gx2 + Ui_Scale(6), Ui_Scale(20),
               button_w, Ui_Scale(22), TRUE);
    MoveWindow(state->split_button, gx2 + Ui_Scale(12) + button_w,
               Ui_Scale(20), button_w, Ui_Scale(22), TRUE);
    MoveWindow(state->reset_button, gx2 + Ui_Scale(18) + button_w * 2,
               Ui_Scale(20), button_w, Ui_Scale(22), TRUE);
    lower_w = (act_w - Ui_Scale(24)) / 3;
    if (lower_w < 1)
        lower_w = 1;
    MoveWindow(state->snapshot_button, gx2 + Ui_Scale(6), Ui_Scale(44),
               lower_w, Ui_Scale(18), TRUE);
    MoveWindow(state->info_bar, gx2 + Ui_Scale(12) + lower_w, Ui_Scale(44),
               lower_w, Ui_Scale(18), TRUE);
    MoveWindow(state->metrics_button,
               gx2 + Ui_Scale(18) + lower_w * 2, Ui_Scale(44),
               lower_w, Ui_Scale(18), TRUE);
    ana_button_w = (ana_w - Ui_Scale(24)) / 3;
    if (ana_button_w < 1)
        ana_button_w = 1;
    MoveWindow(state->btn_ana[0], gx3 + Ui_Scale(6), Ui_Scale(44),
               ana_button_w, Ui_Scale(18), TRUE);
    MoveWindow(state->btn_ana[1], gx3 + Ui_Scale(12) + ana_button_w,
               Ui_Scale(44), ana_button_w, Ui_Scale(18), TRUE);
    MoveWindow(state->btn_ana[2],
               gx3 + Ui_Scale(18) + ana_button_w * 2, Ui_Scale(44),
               ana_button_w, Ui_Scale(18), TRUE);
}

static void v2_paint_control_bar(cmp_v2_t *state, HDC dc)
{
    static const char *titles[4] = {
        "Zoom", "Pan & Sync", "Actions", "Analyze"
    };
    RECT client, bar, frame_rc, text_rc;
    SIZE text_sz;
    HFONT old_font;
    int k, cx;
    if (!GetClientRect(state->hwnd, &client))
        return;
    bar.left = 0;
    bar.top = 0;
    bar.right = client.right;
    bar.bottom = Ui_Scale(V2_TOP_H);
    FillRect(dc, &bar, (HBRUSH)(COLOR_BTNFACE + 1));
    old_font = (HFONT)SelectObject(dc, Compare_Font());
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    for (k = 0; k < 4; k++) {
        if (state->grp_rc[k].right <= state->grp_rc[k].left)
            continue;
        if (!GetTextExtentPoint32A(dc, titles[k], (int)strlen(titles[k]),
                                   &text_sz))
            continue;
        cx = text_sz.cx;
        frame_rc = state->grp_rc[k];
        frame_rc.top += text_sz.cy / 2;
        DrawEdge(dc, &frame_rc, EDGE_ETCHED, BF_RECT);
        text_rc.left = state->grp_rc[k].left + Ui_Scale(8);
        text_rc.top = state->grp_rc[k].top;
        text_rc.right = text_rc.left + cx + Ui_Scale(4);
        text_rc.bottom = text_rc.top + text_sz.cy;
        ExtTextOutA(dc, state->grp_rc[k].left + Ui_Scale(10),
                    state->grp_rc[k].top, ETO_OPAQUE, &text_rc, titles[k],
                    (UINT)strlen(titles[k]), NULL);
    }
    if (old_font)
        SelectObject(dc, old_font);
}

static void v2_zoom_both(cmp_v2_t *state, int direction, int x, int y)
{
    int left_index = v2_image_index(state, 0);
    double next = CmpZoom_Step(state->view[left_index].zoom, direction);
    RECT client;
    int i;
    GetClientRect(state->overlay, &client);
    for (i = 0; i < 2; i++)
        CmpView_ZoomAt(&state->view[i], next, x, y,
                       client.right, client.bottom);
    v2_metrics_schedule(state);
    v2_sync_controls(state);
    v2_update_status(state);
    InvalidateRect(state->overlay, NULL, FALSE);
}

static LRESULT CALLBACK V2OverlayProc(HWND hwnd, UINT message, WPARAM wparam,
                                      LPARAM lparam)
{
    cmp_v2_t *state = v2_state(hwnd);
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
            v2_paint(state, hwnd);
        else {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_SIZE:
        if (state) {
            if (state->need_fit && LOWORD(lparam) > 0 && HIWORD(lparam) > 0)
                v2_fit_views(state);
            else
                v2_metrics_schedule(state);
            InvalidateRect(hwnd, NULL, FALSE);
            v2_update_status(state);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (!state)
            break;
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
        SetFocus(hwnd);
        SetCapture(hwnd);
        state->last = point;
        state->drag = (message == WM_LBUTTONDOWN && state->split_mode) ?
                      V2_DRAG_SPLIT : V2_DRAG_PAN;
        if (state->drag == V2_DRAG_SPLIT) {
            RECT client;
            GetClientRect(hwnd, &client);
            if (client.right > 0)
                state->split_fraction =
                    v2_clamp(point.x / (double)client.right, 0.0, 1.0);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        v2_update_status(state);
        return 0;
    case WM_MOUSEMOVE:
        if (state) {
            point.x = GET_X_LPARAM(lparam);
            point.y = GET_Y_LPARAM(lparam);
            state->pointer = point;
            state->have_pointer = TRUE;
            if (state->drag == V2_DRAG_SPLIT) {
                RECT client;
                GetClientRect(hwnd, &client);
                if (client.right > 0)
                    state->split_fraction =
                        v2_clamp(point.x / (double)client.right, 0.0, 1.0);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (state->drag == V2_DRAG_PAN &&
                       (wparam & (MK_LBUTTON | MK_RBUTTON))) {
                double dx = point.x - state->last.x;
                double dy = point.y - state->last.y;
                int first = state->sync_pan ? 0 :
                            v2_image_index(state, state->pan_side);
                int last = state->sync_pan ? 2 : first + 1;
                int i;
                for (i = first; i < last; i++)
                    CmpView_Pan(&state->view[i], dx, dy);
                v2_metrics_schedule(state);
                state->last = point;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            v2_update_status(state);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (state) {
            state->drag = V2_DRAG_NONE;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            v2_update_status(state);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state)
            state->drag = V2_DRAG_NONE;
        return 0;
    case WM_SETCURSOR:
        if (state && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursor(NULL,
                state->drag == V2_DRAG_PAN ? IDC_HAND :
                (state->split_mode ? IDC_SIZEWE : IDC_HAND)));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL:
        if (state) {
            RECT client;
            POINT screen;
            int width, height;
            screen.x = GET_X_LPARAM(lparam);
            screen.y = GET_Y_LPARAM(lparam);
            point = screen;
            ScreenToClient(hwnd, &point);
            GetClientRect(hwnd, &client);
            width = client.right;
            height = client.bottom;
            state->wheel_acc += GET_WHEEL_DELTA_WPARAM(wparam);
            while (state->wheel_acc >= WHEEL_DELTA ||
                   state->wheel_acc <= -WHEEL_DELTA) {
                int direction = state->wheel_acc > 0 ? 1 : -1;
                int left_index = v2_image_index(state, 0);
                double next = CmpZoom_Step(state->view[left_index].zoom,
                                            direction);
                state->wheel_acc += direction > 0 ? -WHEEL_DELTA : WHEEL_DELTA;
                if (fabs(next - state->view[left_index].zoom) >= 1e-9) {
                    int i;
                    for (i = 0; i < 2; i++)
                        CmpView_ZoomAt(&state->view[i], next, point.x,
                                       point.y, width, height);
                    v2_metrics_schedule(state);
                    v2_sync_controls(state);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            state->pointer = point;
            state->have_pointer = TRUE;
            v2_update_status(state);
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (state) {
            v2_fit_views(state);
            InvalidateRect(hwnd, NULL, FALSE);
            v2_update_status(state);
        }
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK V2WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam)
{
    cmp_v2_t *state = v2_state(hwnd);
    switch (message) {
    case WM_NCCREATE:
        state = (cmp_v2_t *)((CREATESTRUCTA *)lparam)->lpCreateParams;
        state->hwnd = hwnd;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    case WM_CREATE: {
        HINSTANCE instance = ((CREATESTRUCTA *)lparam)->hInstance;
        int i;
        state->label[0] = CreateWindowExA(0, "STATIC", "L: 100%",
            WS_CHILD | WS_VISIBLE, Ui_Scale(12), Ui_Scale(20), Ui_Scale(64),
            Ui_Scale(22), hwnd, NULL, instance, NULL);
        state->track[0] = CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            Ui_Scale(78), Ui_Scale(18), Ui_Scale(170), Ui_Scale(24), hwnd,
            (HMENU)V2_ID_TRACK_L, instance, NULL);
        state->label[1] = CreateWindowExA(0, "STATIC", "R: 100%",
            WS_CHILD | WS_VISIBLE, Ui_Scale(12), Ui_Scale(46), Ui_Scale(64),
            Ui_Scale(22), hwnd, NULL, instance, NULL);
        state->track[1] = CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            Ui_Scale(78), Ui_Scale(44), Ui_Scale(170), Ui_Scale(24), hwnd,
            (HMENU)V2_ID_TRACK_R, instance, NULL);
        state->sync = CreateWindowExA(0, "BUTTON", "Sync pan",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, Ui_Scale(286),
            Ui_Scale(20), Ui_Scale(130), Ui_Scale(22), hwnd,
            (HMENU)V2_ID_SYNC, instance, NULL);
        state->pan_left = CreateWindowExA(0, "BUTTON", "Left",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
            Ui_Scale(286), Ui_Scale(42), Ui_Scale(70), Ui_Scale(22), hwnd,
            (HMENU)V2_ID_LEFT, instance, NULL);
        state->pan_right = CreateWindowExA(0, "BUTTON", "Right",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            Ui_Scale(360), Ui_Scale(42), Ui_Scale(78), Ui_Scale(22), hwnd,
            (HMENU)V2_ID_RIGHT, instance, NULL);
        state->swap_button = CreateWindowExA(0, "BUTTON", "Swap",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, Ui_Scale(490),
            Ui_Scale(24), Ui_Scale(70), Ui_Scale(28),
            hwnd, (HMENU)V2_ID_SWAP, instance, NULL);
        state->split_button = CreateWindowExA(0, "BUTTON", "Split: ON",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            Ui_Scale(566), Ui_Scale(24), Ui_Scale(95), Ui_Scale(28), hwnd,
            (HMENU)V2_ID_SPLIT, instance, NULL);
        state->reset_button = CreateWindowExA(0, "BUTTON", "Reset All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, Ui_Scale(668),
            Ui_Scale(24), Ui_Scale(100), Ui_Scale(28),
            hwnd, (HMENU)V2_ID_RESET, instance, NULL);
        state->snapshot_button = CreateWindowExA(0, "BUTTON", "Snapshot",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, Ui_Scale(770),
            Ui_Scale(43), Ui_Scale(78), Ui_Scale(20),
            hwnd, (HMENU)V2_ID_SNAPSHOT, instance, NULL);
        state->metrics_button = CreateWindowExA(0, "BUTTON", "Metrics",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, Ui_Scale(938),
            Ui_Scale(43), Ui_Scale(90), Ui_Scale(20),
            hwnd, (HMENU)V2_ID_METRICS, instance, NULL);
        state->info_bar = CreateWindowExA(0, "BUTTON", "Info bar",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, Ui_Scale(850),
            Ui_Scale(43), Ui_Scale(86), Ui_Scale(20),
            hwnd, (HMENU)V2_ID_INFO, instance, NULL);
        state->btn_ana[0] = CreateWindowExA(0, "BUTTON", "Sharp",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            Ui_Scale(842), Ui_Scale(43), Ui_Scale(58), Ui_Scale(20), hwnd,
            (HMENU)V2_ID_ANA_SHARP, instance, NULL);
        state->btn_ana[1] = CreateWindowExA(0, "BUTTON", "Texture",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            Ui_Scale(902), Ui_Scale(43), Ui_Scale(66), Ui_Scale(20), hwnd,
            (HMENU)V2_ID_ANA_TEXTURE, instance, NULL);
        state->btn_ana[2] = CreateWindowExA(0, "BUTTON", "Neutral",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            Ui_Scale(970), Ui_Scale(43), Ui_Scale(66), Ui_Scale(20), hwnd,
            (HMENU)V2_ID_ANA_NEUTRAL, instance, NULL);
        state->overlay = CreateWindowExA(0, V2_OVERLAY_CLASS, "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0,
            Ui_Scale(V2_TOP_H), 0, 0,
            hwnd, NULL, instance, state);
        state->status = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, Ui_Scale(4), 0, 0,
            Ui_Scale(V2_BOTTOM_H),
            hwnd, NULL, instance, NULL);
        if (!state->label[0] ||
            !state->label[1] || !state->track[0] || !state->track[1] ||
            !state->sync || !state->pan_left || !state->pan_right ||
            !state->swap_button || !state->split_button ||
            !state->reset_button || !state->snapshot_button ||
            !state->metrics_button || !state->info_bar ||
            !state->btn_ana[0] || !state->btn_ana[1] || !state->btn_ana[2] ||
            !state->overlay || !state->status)
            return -1;
        if (!v2_create_tooltip(state))
            return -1;
        for (i = 0; i < 2; i++) {
            SendMessageA(state->track[i], TBM_SETRANGE, TRUE,
                         MAKELONG(2, 800));
            SendMessageA(state->track[i], TBM_SETTICFREQ, 50, 0);
            SendMessageA(state->track[i], TBM_SETLINESIZE, 0, 1);
            SendMessageA(state->track[i], TBM_SETPAGESIZE, 0, 10);
            SendMessageA(state->track[i], WM_SETFONT,
                         (WPARAM)Compare_Font(), TRUE);
            SendMessageA(state->label[i], WM_SETFONT,
                         (WPARAM)Compare_Font(), TRUE);
        }
        SendMessageA(state->sync, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->pan_left, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->pan_right, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->swap_button, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->split_button, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->reset_button, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->snapshot_button, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->metrics_button, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->info_bar, WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->btn_ana[0], WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->btn_ana[1], WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->btn_ana[2], WM_SETFONT,
                     (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->status, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->pan_left, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(state->split_button, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(state->info_bar, BM_SETCHECK, BST_CHECKED, 0);
        state->show_info = TRUE;
        state->need_fit = TRUE;
        v2_layout(state);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc;
        dc = BeginPaint(hwnd, &paint);
        if (state)
            v2_paint_control_bar(state, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_SIZE:
        if (state) {
            v2_layout(state);
            v2_update_status(state);
            RedrawWindow(hwnd, NULL, NULL,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = Ui_Scale(900);
        limits->ptMinTrackSize.y = Ui_Scale(420);
        return 0;
    }
    case WM_COMMAND:
        if (!state)
            break;
        if (HIWORD(wparam) == BN_CLICKED) {
            switch (LOWORD(wparam)) {
            case V2_ID_SYNC:
                state->sync_pan =
                    SendMessageA(state->sync, BM_GETCHECK, 0, 0) == BST_CHECKED;
                break;
            case V2_ID_LEFT:
                state->pan_side = 0;
                break;
            case V2_ID_RIGHT:
                state->pan_side = 1;
                break;
            case V2_ID_SWAP:
                state->swapped = !state->swapped;
                v2_update_title(state);
                v2_sync_controls(state);
                break;
            case V2_ID_SPLIT:
                state->split_mode =
                    SendMessageA(state->split_button, BM_GETCHECK, 0, 0) ==
                    BST_CHECKED;
                SetWindowTextA(state->split_button,
                               state->split_mode ? "Split: ON" : "Split: OFF");
                break;
            case V2_ID_RESET:
                v2_reset_all(state);
                break;
            case V2_ID_SNAPSHOT:
                v2_snapshot(state, FALSE);
                break;
            case V2_ID_METRICS:
                v2_run_metrics(state);
                break;
            case V2_ID_INFO:
                state->show_info =
                    SendMessageA(state->info_bar, BM_GETCHECK, 0, 0) ==
                    BST_CHECKED;
                break;
            case V2_ID_ANA_SHARP:
                v2_set_mode(state, CMP_ANA_SHARP);
                break;
            case V2_ID_ANA_TEXTURE:
                v2_set_mode(state, CMP_ANA_TEXTURE);
                break;
            case V2_ID_ANA_NEUTRAL:
                v2_set_mode(state, CMP_ANA_NEUTRAL);
                break;
            default:
                break;
            }
            v2_update_status(state);
            InvalidateRect(state->overlay, NULL, FALSE);
            SetFocus(state->overlay);
            return 0;
        }
        break;
    case WM_HSCROLL:
        if (state && ((HWND)lparam == state->track[0] ||
                      (HWND)lparam == state->track[1])) {
            int side = (HWND)lparam == state->track[0] ? 0 : 1;
            int index = v2_image_index(state, side);
            int position = (int)SendMessageA(state->track[side], TBM_GETPOS, 0, 0);
            state->view[index].zoom =
                CmpZoom_Quantize(position / 100.0);
            v2_sync_controls(state);
            v2_update_status(state);
            InvalidateRect(state->overlay, NULL, FALSE);
            v2_metrics_schedule(state);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (state && state->overlay)
            SendMessageA(state->overlay, message, wparam, lparam);
        return 0;
    case CMPM_KEY:
        if (!state)
            return 0;
        {
        BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wparam == 'M' && ctrl) {
            v2_run_metrics(state);
            return 1;
        }
        if ((HWND)GetFocus() == state->track[0] ||
            (HWND)GetFocus() == state->track[1]) {
            if (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_HOME ||
                wparam == VK_END || wparam == VK_PRIOR || wparam == VK_NEXT)
                return 0;
        }
        if (wparam == VK_ESCAPE ||
            (wparam == 'W' && ctrl)) {
            DestroyWindow(hwnd);
            return 1;
        }
        if (wparam == 'S' && ctrl && shift) {
            v2_snapshot(state, FALSE);
            return 1;
        }
        if (wparam == 'S' && ctrl) {
            v2_snapshot(state, FALSE);
            return 1;
        }
        if (wparam == 'C' && ctrl) {
            v2_snapshot(state, TRUE);
            return 1;
        }
        if (!ctrl && wparam == 'S') {
            SendMessageA(state->swap_button, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == 'M') {
            SendMessageA(state->split_button, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == 'P') {
            SendMessageA(state->sync, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == '[') {
            SendMessageA(state->pan_left, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == ']') {
            SendMessageA(state->pan_right, BM_CLICK, 0, 0);
            return 1;
        }
        if (!ctrl && wparam == '0') {
            v2_reset_all(state);
            return 1;
        }
        if (!ctrl && !alt &&
            (wparam == 'E' || wparam == 'T' || wparam == 'N')) {
            cmp_ana_t mode = wparam == 'E' ? CMP_ANA_SHARP :
                             (wparam == 'T' ? CMP_ANA_TEXTURE :
                                              CMP_ANA_NEUTRAL);
            v2_set_mode(state, mode);
            return 1;
        }
        if (wparam == VK_ADD || wparam == VK_OEM_PLUS ||
            wparam == VK_SUBTRACT || wparam == VK_OEM_MINUS) {
            if (ctrl)
                return 0;
            RECT client;
            GetClientRect(state->overlay, &client);
            v2_zoom_both(state,
                (wparam == VK_ADD || wparam == VK_OEM_PLUS) ? 1 : -1,
                client.right / 2, client.bottom / 2);
            return 1;
        }
        return 0;
        }
    case CMPM_METRICS:
        return state ? v2_run_metrics(state) : FALSE;
    case WM_TIMER:
        if (state && wparam == CMP_TID_METRIC) {
            KillTimer(hwnd, CMP_TID_METRIC);
            v2_metrics_update(state);
            InvalidateRect(state->overlay, NULL, FALSE);
            return 0;
        }
        break;
    case WM_APP_METRICS_DONE:
        if (state && state->metrics_job == (metrics_async_job_t *)wparam) {
            Metrics_CloseProgress(state->progress);
            state->progress = NULL;
            Compare_CompleteMetrics(state->metrics_job, NULL);
            Metrics_ReleaseAsync(state->metrics_job);
            state->metrics_job = NULL;
            EnableWindow(state->metrics_button, TRUE);
            SetWindowTextA(state->metrics_button, "Metrics");
            SetFocus(state->overlay);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if (state) {
            if (state->tooltip) {
                DestroyWindow(state->tooltip);
                state->tooltip = NULL;
            }
            v2_release(state);
        }
        return 0;
    case WM_NCDESTROY:
        if (state) {
            v2_release(state);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            free(state);
        }
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

BOOL CompareV2_Register(HINSTANCE instance)
{
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = V2WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = V2_CLASS;
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    wc.lpfnWndProc = V2OverlayProc;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.style = CS_DBLCLKS;
    wc.lpszClassName = V2_OVERLAY_CLASS;
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    return TRUE;
}

HWND CompareV2_OpenMode(cmp_image_t *left, cmp_image_t *right,
                        cmp_ana_t mode)
{
    cmp_v2_t *state;
    HWND hwnd;
    POINT cursor;
    HMONITOR monitor;
    MONITORINFO info;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1200, height = 800;
    if (!left || !right || !left->img.valid || !right->img.valid ||
        mode < CMP_ANA_NONE || mode >= CMP_ANA_COUNT)
        return NULL;
    state = (cmp_v2_t *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->image[0] = CmpImage_Ref(left);
    state->image[1] = CmpImage_Ref(right);
    state->split_mode = TRUE;
    state->split_fraction = 0.5;
    state->need_fit = TRUE;
    state->pan_side = 0;
    if (GetCursorPos(&cursor)) {
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (GetMonitorInfoA(monitor, &info)) {
            x = info.rcWork.left;
            y = info.rcWork.top;
            width = info.rcWork.right - info.rcWork.left;
            height = info.rcWork.bottom - info.rcWork.top;
        }
    }
    hwnd = CreateWindowExA(0, V2_CLASS, "Compare",
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
    if (mode != CMP_ANA_NONE)
        v2_set_mode(state, mode);
    return hwnd;
}

HWND CompareV2_Open(cmp_image_t *left, cmp_image_t *right)
{
    return CompareV2_OpenMode(left, right, CMP_ANA_NONE);
}
