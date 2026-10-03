#include "canvas.h"

#include <stdio.h>
#include <string.h>
#include <windowsx.h>

#include "app.h"
#include "app_messages.h"
#include "detect.h"

#define WM_CANVAS_BUILD_PYRAMID (WM_APP + 1)

static BOOL g_panning;
static POINT g_pan_last;
static DWORD s_last_preview_tick;
static BOOL s_has_preview;

void Canvas_NavigationStarted(void)
{
    if (s_has_preview)
        s_has_preview = FALSE;
}

static void build_pyramid(HWND hwnd)
{
    g_app.pyramid_pending = FALSE;
    if (g_app.img.valid && !g_app.pyramid_attempted) {
        g_app.pyramid_attempted = TRUE;
        if (ViewPyr_Build(&g_app.pyramid, &g_app.img)) {
            g_app.pyramid_gen = g_app.img_gen;
            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            OutputDebugStringA("ROI Analyzer: image pyramid allocation failed; using full-resolution rendering.\n");
        }
    }
}

void Canvas_BuildPyramidNow(void)
{
    if (g_app.hwnd_canvas)
        SendMessage(g_app.hwnd_canvas, WM_CANVAS_BUILD_PYRAMID, 0, 0);
}

static void draw_outline(HDC hdc, const RECT *rc, COLORREF color, int width,
                         int pen_style)
{
    HPEN pen = CreatePen(pen_style, width, color);
    HGDIOBJ old_pen;
    HGDIOBJ old_brush;

    if (!pen)
        return;
    old_pen = SelectObject(hdc, pen);
    old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void draw_label(HDC hdc, int x, int y, int number, BOOL centered)
{
    char text[16];
    SIZE size;
    RECT rc;

    _snprintf(text, sizeof(text), "%d", number);
    text[sizeof(text) - 1] = '\0';
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &size);
    if (centered) {
        x -= (size.cx + 6) / 2;
        y -= (size.cy + 4) / 2;
    }
    rc.left = x;
    rc.top = y;
    rc.right = x + size.cx + 6;
    rc.bottom = y + size.cy + 4;
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, RGB(0, 0, 0));
    SetTextColor(hdc, RGB(255, 255, 255));
    ExtTextOutA(hdc, x + 3, y + 2, ETO_OPAQUE, &rc, text,
                (UINT)strlen(text), NULL);
}

static void draw_roi_overlay(HDC hdc)
{
    int i, pass;

    if (!g_app.img.valid || g_app.view.scale <= 0.0f)
        return;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < g_app.rois.count; i++) {
            const roi_item_t *item = &g_app.rois.items[i];
            RECT wr;
            int number = ROI_SourceIndex(&g_app.rois, i) + 1;
            BOOL selected = i == g_app.rois.selected;
            BOOL grid = item->source != ROI_SRC_MANUAL;
            if (grid != (pass == 0))
                continue;
            if (grid && item->source != ROI_ModeSource(g_app.mode))
                continue;
            View_RectToWindow(&g_app.view, item->rc, &wr);
            draw_outline(hdc, &wr,
                         selected ? RGB(255, 0, 255) :
                         (grid ? RGB(0, 255, 255) : RGB(255, 255, 0)),
                         selected ? 3 : (grid ? 1 : 2), PS_SOLID);
            if (grid) {
                draw_label(hdc, (wr.left + wr.right) / 2,
                           (wr.top + wr.bottom) / 2, number, TRUE);
            } else {
                int width = wr.right - wr.left;
                draw_label(hdc, wr.left + 2,
                           width < 18 ? wr.top - 18 : wr.top + 2,
                           number, FALSE);
            }
        }
    }
    if (g_app.drag.dragging) {
        RECT rc;
        POINT a = g_app.drag.anchor_img;
        POINT b = g_app.drag.cur_img;
        rc.left = a.x < b.x ? a.x : b.x;
        rc.top = a.y < b.y ? a.y : b.y;
        rc.right = a.x > b.x ? a.x : b.x;
        rc.bottom = a.y > b.y ? a.y : b.y;
        View_RectToWindow(&g_app.view, rc, &rc);
        draw_outline(hdc, &rc, RGB(255, 255, 255), 1, PS_DOT);
    }
}

static void draw_detection_overlay(HDC hdc)
{
    const yolo_detection_t *detections;
    size_t count = Detect_GetResults(&detections);
    size_t i;

    if (g_app.view.scale <= 0.0f)
        return;
    for (i = 0; i < count; ++i) {
        RECT image_rect, window_rect;
        char label[64];
        SIZE text_size;
        RECT label_rect;
        int x, y;

        image_rect.left = (LONG)detections[i].x1;
        image_rect.top = (LONG)detections[i].y1;
        image_rect.right = (LONG)detections[i].x2;
        image_rect.bottom = (LONG)detections[i].y2;
        View_RectToWindow(&g_app.view, image_rect, &window_rect);
        draw_outline(hdc, &window_rect, RGB(255, 0, 255), 2, PS_SOLID);
        _snprintf(label, sizeof(label), "color-chart %.2f",
                  (double)detections[i].score);
        label[sizeof(label) - 1] = '\0';
        GetTextExtentPoint32A(hdc, label, (int)strlen(label), &text_size);
        x = window_rect.left;
        y = window_rect.top - text_size.cy - 4;
        if (y < 0)
            y = window_rect.top;
        label_rect.left = x;
        label_rect.top = y;
        label_rect.right = x + text_size.cx + 6;
        label_rect.bottom = y + text_size.cy + 4;
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(0, 0, 0));
        SetTextColor(hdc, RGB(255, 255, 255));
        ExtTextOutA(hdc, x + 3, y + 2, ETO_OPAQUE, &label_rect, label,
                    (UINT)strlen(label), NULL);
    }
}

static void cc_overlay_draw(HDC hdc)
{
    size_t i;

    if (!g_app.img.valid || !g_app.show_patch_grid ||
        !g_app.locate_result_valid ||
        (LONG)g_app.locate_result.seq != Detect_ImageSeq() ||
        !g_app.locate_result.success || g_app.view.scale <= 0.0f) {
        return;
    }
    for (i = 0; i < CC_ROI_COUNT; ++i) {
        const cc_roi_t *roi = &g_app.locate_result.rois[i];
        RECT image_rect;
        RECT window_rect;
        RECT outer_rect;

        if (!roi->valid)
            continue;
        image_rect.left = (LONG)roi->display_x1;
        image_rect.top = (LONG)roi->display_y1;
        image_rect.right = (LONG)roi->display_x2;
        image_rect.bottom = (LONG)roi->display_y2;
        View_RectToWindow(&g_app.view, image_rect, &window_rect);
        outer_rect.left = window_rect.left - 1;
        outer_rect.top = window_rect.top - 1;
        outer_rect.right = window_rect.right + 1;
        outer_rect.bottom = window_rect.bottom + 1;
        draw_outline(hdc, &outer_rect, RGB(0, 0, 0), 1, PS_SOLID);
        draw_outline(hdc, &window_rect, RGB(0, 255, 255), 1, PS_SOLID);
        draw_label(hdc, window_rect.left + 2, window_rect.top + 2,
                   roi->id, FALSE);
    }
}

BOOL Canvas_Register(HINSTANCE instance)
{
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = CanvasWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "RoiAnalyzerCanvas";
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_TIMER:
        if (wparam == IDT_DETECT_DELAY) {
            if (!PostMessageA(g_app.hwnd_main, WM_TIMER, wparam, 0))
                OutputDebugStringA("ROI Analyzer: could not dispatch detection timer.\n");
            return 0;
        }
        break;
    case WM_CANVAS_BUILD_PYRAMID:
        build_pyramid(hwnd);
        return 0;
    case WM_SIZE:
        View_Update(&g_app.view, LOWORD(lparam), HIWORD(lparam),
                    g_app.img.valid ? g_app.img.w : 0,
                    g_app.img.valid ? g_app.img.h : 0, g_app.view.zoom);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        App_FlushPending();
        if (!g_app.img.valid)
            return 0;
        SetFocus(hwnd);
        if (g_app.mode == MODE_DRAG) {
            BOOL additive = g_app.multi || (GetKeyState(VK_CONTROL) & 0x8000);
            if (ROI_OnLDown(&g_app.rois, &g_app.drag, &g_app.img,
                            &g_app.view, p, additive)) {
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            POINT ip;
            if (View_ToImage(&g_app.view, g_app.img.w, g_app.img.h, p, &ip)) {
                App_SelectROI(ROI_HitTest(&g_app.rois, ip));
            } else
                App_SelectROI(-1);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        if (g_panning) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            View_Pan(&g_app.view, &g_app.img, rc.right, rc.bottom,
                     p.x - g_pan_last.x, p.y - g_pan_last.y);
            g_pan_last = p;
            InvalidateRect(hwnd, NULL, FALSE);
            App_UpdateStatus();
            return 0;
        }
        if (g_app.drag.dragging &&
            ROI_OnMove(&g_app.drag, &g_app.img, &g_app.view, p)) {
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_app.mode == MODE_DRAG && g_app.show_hist &&
                g_app.img.valid) {
                DWORD tick = GetTickCount();
                if (tick - s_last_preview_tick > 80) {
                    RECT img_rc;
                    POINT a = g_app.drag.anchor_img;
                    POINT b = g_app.drag.cur_img;
                    img_rc.left = a.x < b.x ? a.x : b.x;
                    img_rc.top = a.y < b.y ? a.y : b.y;
                    img_rc.right = a.x > b.x ? a.x : b.x;
                    img_rc.bottom = a.y > b.y ? a.y : b.y;
                    if (img_rc.left < img_rc.right &&
                        img_rc.top < img_rc.bottom) {
                        App_PreviewHistogram(img_rc);
                        s_has_preview = TRUE;
                        s_last_preview_tick = tick;
                    }
                }
            }
        }
        App_UpdateStatus();
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        BOOL was_click = FALSE;
        BOOL had_drag = g_app.drag.dragging;
        BOOL changed = ROI_OnLUp(&g_app.rois, &g_app.drag, &g_app.img,
                                 &g_app.view, p, &was_click);
        if (had_drag) {
            ReleaseCapture();
            if (changed && was_click)
                App_SelectROI(g_app.rois.selected);
            else
                App_RoiChanged();
            if (!changed && !was_click)
                MessageBoxA(g_app.hwnd_main, "Could not add ROI (out of memory).",
                            "ROI Analyzer", MB_OK | MB_ICONERROR);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        if (g_app.img.valid) {
            g_panning = TRUE;
            g_pan_last.x = GET_X_LPARAM(lparam);
            g_pan_last.y = GET_Y_LPARAM(lparam);
            SetFocus(hwnd);
            SetCapture(hwnd);
        }
        return 0;
    case WM_RBUTTONUP:
        if (g_panning) {
            g_panning = FALSE;
            ReleaseCapture();
            App_UpdateStatus();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) {
            RECT rc;
            POINT anchor = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            float zoom = g_app.view.zoom;
            int steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            GetClientRect(hwnd, &rc);
            ScreenToClient(hwnd, &anchor);
            while (steps > 0) {
                zoom *= 1.1f;
                steps--;
            }
            while (steps < 0) {
                zoom /= 1.1f;
                steps++;
            }
            View_SetZoom(&g_app.view, &g_app.img, rc.right, rc.bottom,
                         zoom, anchor);
            InvalidateRect(hwnd, NULL, FALSE);
            App_UpdateStatus();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && g_app.drag.dragging) {
            g_app.drag.dragging = FALSE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            App_UpdateHistogram();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            App_SelectROI(-1);
            return 0;
        }
        if (wparam == '1' || wparam == '2' || wparam == '3' ||
            wparam == 'M' || wparam == 'O' ||
            (wparam == 'E' && (GetKeyState(VK_CONTROL) & 0x8000)) ||
            wparam == VK_DELETE || wparam == 'C' ||
            wparam == VK_ADD || wparam == VK_SUBTRACT ||
            wparam == VK_OEM_PLUS || wparam == VK_OEM_MINUS) {
            int command = 0;
            if (wparam == '1') command = 111;
            else if (wparam == '2') command = 112;
            else if (wparam == '3') command = 113;
            else if (wparam == 'M') command = 114;
            else if (wparam == 'O') command = 101;
            else if (wparam == 'E') command = 102;
            else if (wparam == VK_DELETE) command = 121;
            else if (wparam == 'C')
                command = (GetKeyState(VK_SHIFT) & 0x8000) ? 123 : 122;
            else if (wparam == VK_ADD || wparam == VK_OEM_PLUS)
                command = 141;
            else
                command = 142;
            SendMessage(GetParent(hwnd), WM_COMMAND, (WPARAM)command, 0);
            return 0;
        }
        if (wparam == '0') {
            RECT rc;
            GetClientRect(hwnd, &rc);
            View_Reset(&g_app.view, &g_app.img, rc.right, rc.bottom);
            InvalidateRect(hwnd, NULL, FALSE);
            App_UpdateStatus();
            return 0;
        }
        if ((wparam == VK_LEFT || wparam == VK_RIGHT ||
             wparam == VK_UP || wparam == VK_DOWN) &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            int distance = (GetKeyState(VK_SHIFT) & 0x8000) ? 100 : 20;
            int dx = 0, dy = 0;
            RECT rc;
            if (!g_app.img.valid)
                return 0;
            if (wparam == VK_LEFT)
                dx = -distance;
            else if (wparam == VK_RIGHT)
                dx = distance;
            else if (wparam == VK_UP)
                dy = -distance;
            else
                dy = distance;
            GetClientRect(hwnd, &rc);
            View_Pan(&g_app.view, &g_app.img, rc.right, rc.bottom, dx, dy);
            InvalidateRect(hwnd, NULL, FALSE);
            App_UpdateStatus();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        LARGE_INTEGER paint_started;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HDC mem;
        HBITMAP bitmap, old_bitmap;
        QueryPerformanceCounter(&paint_started);
        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(hdc);
        bitmap = CreateCompatibleBitmap(hdc,
                                       rc.right > 0 ? rc.right : 1,
                                       rc.bottom > 0 ? rc.bottom : 1);
        if (!mem || !bitmap) {
            if (bitmap)
                DeleteObject(bitmap);
            if (mem)
                DeleteDC(mem);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_APPWORKSPACE));
            EndPaint(hwnd, &ps);
            if (g_app.paint_pending) {
                g_app.paint_ms = App_Ms(paint_started);
                g_app.paint_pending = FALSE;
                App_UpdateStatus();
            }
            return 0;
        }
        old_bitmap = (HBITMAP)SelectObject(mem, bitmap);
        {
            HBRUSH background = CreateSolidBrush(RGB(32, 32, 32));
            if (background) {
                FillRect(mem, &rc, background);
                DeleteObject(background);
            }
        }
        if (g_app.img.valid) {
            if (g_app.pyramid.count > 0 &&
                g_app.pyramid_gen == g_app.img_gen)
                View_DrawImagePyramid(mem, &g_app.view, &g_app.img,
                                      &g_app.pyramid);
            else
                View_DrawImage(mem, &g_app.view, &g_app.img);
            draw_detection_overlay(mem);
            cc_overlay_draw(mem);
            draw_roi_overlay(mem);
        }
        BitBlt(hdc, rc.left, rc.top, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        if (g_app.img.valid && !g_app.pyramid_attempted &&
            !g_app.pyramid_pending) {
            g_app.pyramid_pending = TRUE;
            PostMessage(hwnd, WM_CANVAS_BUILD_PYRAMID, 0, 0);
        }
        if (g_app.paint_pending) {
            g_app.paint_ms = App_Ms(paint_started);
            g_app.paint_pending = FALSE;
            App_UpdateStatus();
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}
