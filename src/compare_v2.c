#include "compare.h"

#include <commctrl.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V2_TOP_H       76
#define V2_GROUP_H     64
#define V2_BOTTOM_H     28
#define V2_ID_TRACK_L  4201
#define V2_ID_TRACK_R  4202
#define V2_ID_SYNC     4203
#define V2_ID_LEFT     4204
#define V2_ID_RIGHT    4205
#define V2_ID_SWAP     4206
#define V2_ID_SPLIT    4207
#define V2_ID_RESET    4208

enum { V2_DRAG_NONE = 0, V2_DRAG_SPLIT, V2_DRAG_PAN };

typedef struct {
    HWND hwnd;
    HWND overlay;
    HWND status;
    HWND track[2];
    HWND label[2];
    HWND grp[3];
    HWND sync;
    HWND pan_left;
    HWND pan_right;
    HWND swap_button;
    HWND split_button;
    HWND reset_button;
    cmp_image_t *image[2];
    cmp_view_t view[2];
    BOOL swapped;
    BOOL split_mode;
    BOOL sync_pan;
    int pan_side;
    double split_fraction;
    BOOL need_fit;
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
    CmpReg_Remove(state->hwnd);
    state->registered = FALSE;
    v2_free_backbuffer(state);
    CmpImage_Unref(state->image[0]);
    CmpImage_Unref(state->image[1]);
    state->image[0] = state->image[1] = NULL;
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

static void v2_paint(cmp_v2_t *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client, viewport, left_clip, right_clip;
    LARGE_INTEGER start, end, frequency;
    int split, left_index, right_index;
    QueryPerformanceCounter(&start);
    GetClientRect(hwnd, &client);
    if (v2_prepare_backbuffer(state, dc, client.right, client.bottom)) {
        FillRect(state->mem_dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
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
        Cmp_Blit(state->mem_dc, state->image[left_index],
                 &state->view[left_index], &viewport, &left_clip);
        Cmp_Blit(state->mem_dc, state->image[right_index],
                 &state->view[right_index], &viewport, &right_clip);
        if (client.right > 0) {
            HBRUSH yellow = CreateSolidBrush(RGB(255, 220, 0));
            HGDIOBJ old_brush = SelectObject(state->mem_dc, yellow);
            int line_left = split - 1;
            int line_right = split + 1;
            if (line_left < 0) {
                line_left = 0;
                line_right = client.right < 2 ? client.right : 2;
            }
            if (line_right > client.right) {
                line_right = client.right;
                line_left = client.right < 2 ? 0 : client.right - 2;
            }
            if (line_right > line_left)
                PatBlt(state->mem_dc, line_left, 0, line_right - line_left,
                       client.bottom, PATCOPY);
            SelectObject(state->mem_dc, old_brush);
            DeleteObject(yellow);
        }
        {
            char left_label[MAX_PATH + 8], right_label[MAX_PATH + 8];
            RECT label = { 8, 8, client.right / 2, 32 };
            HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
            SetBkMode(state->mem_dc, OPAQUE);
            SetBkColor(state->mem_dc, RGB(0, 0, 0));
            SetTextColor(state->mem_dc, RGB(255, 255, 255));
            SelectObject(state->mem_dc, Compare_Font());
            _snprintf(left_label, sizeof(left_label), "A: %s",
                      state->image[left_index]->name);
            _snprintf(right_label, sizeof(right_label), "B: %s",
                      state->image[right_index]->name);
            left_label[sizeof(left_label) - 1] = '\0';
            right_label[sizeof(right_label) - 1] = '\0';
            FillRect(state->mem_dc, &label, black);
            DrawTextA(state->mem_dc, left_label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
            label.left = client.right / 2;
            label.right = client.right - 8;
            FillRect(state->mem_dc, &label, black);
            DrawTextA(state->mem_dc, right_label, -1, &label,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
            DeleteObject(black);
        }
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

static void v2_layout(cmp_v2_t *state)
{
    RECT client;
    int width, gz, zoom_w, gx1, pan_w, gx2, act_w, track_w;
    if (!GetClientRect(state->hwnd, &client))
        return;
    width = client.right;
    /* Proportional bar: Zoom takes half the window, Pan Sync and Actions
       split the remaining half. */
    gz = 4;
    zoom_w = width / 2 - 2;
    gx1 = gz + zoom_w + 4;
    pan_w = (width - gx1 - 4) / 2 - 2;
    gx2 = gx1 + pan_w + 4;
    act_w = width - gx2 - 4;
    MoveWindow(state->overlay, 0, V2_TOP_H, width,
               client.bottom - V2_TOP_H - V2_BOTTOM_H, TRUE);
    MoveWindow(state->status, 0, client.bottom - V2_BOTTOM_H,
               width, V2_BOTTOM_H, TRUE);
    MoveWindow(state->grp[0], gz, 2, zoom_w, V2_GROUP_H, TRUE);
    MoveWindow(state->grp[1], gx1, 2, pan_w, V2_GROUP_H, TRUE);
    MoveWindow(state->grp[2], gx2, 2, act_w, V2_GROUP_H, TRUE);
    /* Zoom tracks stretch with the half-width box. */
    track_w = zoom_w - 45 - 10;
    if (track_w < 80)
        track_w = 80;
    MoveWindow(state->label[0], gz + 8, 20, 32, 22, TRUE);
    MoveWindow(state->track[0], gz + 45, 18, track_w, 24, TRUE);
    MoveWindow(state->label[1], gz + 8, 44, 32, 22, TRUE);
    MoveWindow(state->track[1], gz + 45, 42, track_w, 24, TRUE);
    MoveWindow(state->sync, gx1 + 8, 20, 130, 22, TRUE);
    MoveWindow(state->pan_left, gx1 + 8, 42, 70, 22, TRUE);
    MoveWindow(state->pan_right, gx1 + 82, 42, 78, 22, TRUE);
    /* Action buttons left-anchored and compacted to fit a quarter width. */
    MoveWindow(state->swap_button, gx2 + 6, 24, 60, 28, TRUE);
    MoveWindow(state->split_button, gx2 + 70, 24, 84, 28, TRUE);
    MoveWindow(state->reset_button, gx2 + 158, 24, 84, 28, TRUE);
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
        state->grp[0] = CreateWindowExA(0, "BUTTON", "Zoom",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 4, 2, 260, V2_GROUP_H,
            hwnd, NULL, instance, NULL);
        state->grp[1] = CreateWindowExA(0, "BUTTON", "Pan & Sync",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 270, 2, 205, V2_GROUP_H,
            hwnd, NULL, instance, NULL);
        state->grp[2] = CreateWindowExA(0, "BUTTON", "Actions",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 481, 2, 350, V2_GROUP_H,
            hwnd, NULL, instance, NULL);
        state->label[0] = CreateWindowExA(0, "STATIC", "L: 100%",
            WS_CHILD | WS_VISIBLE, 12, 20, 32, 22, hwnd, NULL, instance, NULL);
        state->track[0] = CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            45, 18, 170, 24, hwnd, (HMENU)V2_ID_TRACK_L, instance, NULL);
        state->label[1] = CreateWindowExA(0, "STATIC", "R: 100%",
            WS_CHILD | WS_VISIBLE, 12, 44, 32, 22, hwnd, NULL, instance, NULL);
        state->track[1] = CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            45, 42, 170, 24, hwnd, (HMENU)V2_ID_TRACK_R, instance, NULL);
        state->sync = CreateWindowExA(0, "BUTTON", "Sync pan",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 286, 20, 130, 22,
            hwnd, (HMENU)V2_ID_SYNC, instance, NULL);
        state->pan_left = CreateWindowExA(0, "BUTTON", "Left",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
            286, 42, 70, 22, hwnd, (HMENU)V2_ID_LEFT, instance, NULL);
        state->pan_right = CreateWindowExA(0, "BUTTON", "Right",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            360, 42, 78, 22, hwnd, (HMENU)V2_ID_RIGHT, instance, NULL);
        state->swap_button = CreateWindowExA(0, "BUTTON", "Swap",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 490, 24, 70, 28,
            hwnd, (HMENU)V2_ID_SWAP, instance, NULL);
        state->split_button = CreateWindowExA(0, "BUTTON", "Split: ON",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            566, 24, 95, 28, hwnd, (HMENU)V2_ID_SPLIT, instance, NULL);
        state->reset_button = CreateWindowExA(0, "BUTTON", "Reset All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 668, 24, 100, 28,
            hwnd, (HMENU)V2_ID_RESET, instance, NULL);
        state->overlay = CreateWindowExA(0, V2_OVERLAY_CLASS, "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, V2_TOP_H, 0, 0,
            hwnd, NULL, instance, state);
        state->status = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 4, 0, 0, V2_BOTTOM_H,
            hwnd, NULL, instance, NULL);
        if (!state->grp[0] || !state->grp[1] || !state->grp[2] ||
            !state->label[0] ||
            !state->label[1] || !state->track[0] || !state->track[1] ||
            !state->sync || !state->pan_left || !state->pan_right ||
            !state->swap_button || !state->split_button ||
            !state->reset_button || !state->overlay || !state->status)
            return -1;
        SendMessageA(state->grp[0], WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->grp[1], WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->grp[2], WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
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
        SendMessageA(state->status, WM_SETFONT, (WPARAM)Compare_Font(), TRUE);
        SendMessageA(state->pan_left, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(state->split_button, BM_SETCHECK, BST_CHECKED, 0);
        state->need_fit = TRUE;
        v2_layout(state);
        return 0;
    }
    case WM_SIZE:
        if (state) {
            v2_layout(state);
            v2_update_status(state);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = 760;
        limits->ptMinTrackSize.y = 420;
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
        if ((HWND)GetFocus() == state->track[0] ||
            (HWND)GetFocus() == state->track[1]) {
            if (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_HOME ||
                wparam == VK_END || wparam == VK_PRIOR || wparam == VK_NEXT)
                return 0;
        }
        if (wparam == VK_ESCAPE ||
            (wparam == 'W' && (GetKeyState(VK_CONTROL) & 0x8000))) {
            DestroyWindow(hwnd);
            return 1;
        }
        if (wparam == 'S') {
            SendMessageA(state->swap_button, BM_CLICK, 0, 0);
            return 1;
        }
        if (wparam == 'M') {
            SendMessageA(state->split_button, BM_CLICK, 0, 0);
            return 1;
        }
        if (wparam == 'P') {
            SendMessageA(state->sync, BM_CLICK, 0, 0);
            return 1;
        }
        if (wparam == '[') {
            SendMessageA(state->pan_left, BM_CLICK, 0, 0);
            return 1;
        }
        if (wparam == ']') {
            SendMessageA(state->pan_right, BM_CLICK, 0, 0);
            return 1;
        }
        if (wparam == '0') {
            v2_reset_all(state);
            return 1;
        }
        if (wparam == VK_ADD || wparam == VK_OEM_PLUS ||
            wparam == VK_SUBTRACT || wparam == VK_OEM_MINUS) {
            RECT client;
            GetClientRect(state->overlay, &client);
            v2_zoom_both(state,
                (wparam == VK_ADD || wparam == VK_OEM_PLUS) ? 1 : -1,
                client.right / 2, client.bottom / 2);
            return 1;
        }
        return 0;
    case WM_DESTROY:
        if (state)
            v2_release(state);
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

HWND CompareV2_Open(cmp_image_t *left, cmp_image_t *right)
{
    cmp_v2_t *state;
    HWND hwnd;
    POINT cursor;
    HMONITOR monitor;
    MONITORINFO info;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1200, height = 800;
    if (!left || !right || !left->img.valid || !right->img.valid)
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
    return hwnd;
}
