#include "compare.h"

#include <math.h>
#include <stdlib.h>

static HWND s_windows[CMP_MAX_WINDOWS];
static int s_window_count;
static HFONT s_font;

static double dmin(double a, double b)
{
    return a < b ? a : b;
}

static double dmax(double a, double b)
{
    return a > b ? a : b;
}

static double clamp_double(double value, double minimum, double maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

HFONT Compare_Font(void)
{
    return s_font ? s_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

void Compare_SetFont(HFONT font)
{
    s_font = font;
}

BOOL Compare_CanOpen(int need)
{
    return need > 0 && CmpImage_LiveCount() <= CMP_MAX_LIVE_IMG - need;
}

BOOL CmpReg_Add(HWND hwnd)
{
    int i;
    for (i = 0; i < s_window_count; i++)
        if (s_windows[i] == hwnd)
            return TRUE;
    if (s_window_count >= CMP_MAX_WINDOWS)
        return FALSE;
    s_windows[s_window_count++] = hwnd;
    return TRUE;
}

void CmpReg_Remove(HWND hwnd)
{
    int i;
    for (i = 0; i < s_window_count; i++) {
        if (s_windows[i] == hwnd) {
            s_windows[i] = s_windows[--s_window_count];
            return;
        }
    }
}

BOOL Compare_PreTranslate(MSG *msg)
{
    HWND root;
    int i;
    if (!msg || msg->message != WM_KEYDOWN || !msg->hwnd)
        return FALSE;
    root = GetAncestor(msg->hwnd, GA_ROOT);
    for (i = 0; i < s_window_count; i++) {
        if (s_windows[i] == root)
            return SendMessageA(root, CMPM_KEY, msg->wParam,
                                (LPARAM)msg->hwnd) != 0;
    }
    return FALSE;
}

void Compare_CloseAll(void)
{
    int guard = CMP_MAX_WINDOWS * 2;
    while (s_window_count > 0 && guard-- > 0) {
        HWND hwnd = s_windows[s_window_count - 1];
        if (!DestroyWindow(hwnd))
            CmpReg_Remove(hwnd);
    }
}

double CmpZoom_Quantize(double zoom)
{
    if (!isfinite(zoom))
        zoom = 1.0;
    zoom = floor(zoom * 100.0 + 0.5) / 100.0;
    return clamp_double(zoom, CMP_ZOOM_MIN, CMP_ZOOM_MAX);
}

double CmpZoom_Step(double zoom, int direction)
{
    double next;
    zoom = CmpZoom_Quantize(zoom);
    next = CmpZoom_Quantize(direction > 0 ?
                             zoom * CMP_ZOOM_STEP : zoom / CMP_ZOOM_STEP);
    if (fabs(next - zoom) < 1e-9)
        next = CmpZoom_Quantize(zoom + (direction > 0 ? 0.01 : -0.01));
    if ((zoom < 1.0 && next > 1.0) ||
        (zoom > 1.0 && next < 1.0))
        next = 1.0;
    return next;
}

void CmpView_Fit(cmp_view_t *view, int image_w, int image_h,
                 int view_w, int view_h, double margin)
{
    double zoom = 1.0;
    if (!view)
        return;
    if (image_w > 0 && image_h > 0 && view_w > 0 && view_h > 0) {
        zoom = dmin((double)view_w / image_w,
                    (double)view_h / image_h) * margin;
        zoom = floor(zoom * 100.0) / 100.0;
    }
    view->zoom = clamp_double(zoom, CMP_ZOOM_MIN, CMP_ZOOM_MAX);
    view->u = image_w * 0.5;
    view->v = image_h * 0.5;
}

void CmpView_ZoomAt(cmp_view_t *view, double zoom, double x, double y,
                    int view_w, int view_h)
{
    double dx, dy, image_x, image_y;
    if (!view || view->zoom <= 0.0)
        return;
    zoom = CmpZoom_Quantize(zoom);
    dx = x - view_w * 0.5;
    dy = y - view_h * 0.5;
    image_x = view->u + dx / view->zoom;
    image_y = view->v + dy / view->zoom;
    view->zoom = zoom;
    view->u = image_x - dx / zoom;
    view->v = image_y - dy / zoom;
}

void CmpView_Pan(cmp_view_t *view, double dx, double dy)
{
    if (!view || view->zoom <= 0.0)
        return;
    view->u -= dx / view->zoom;
    view->v -= dy / view->zoom;
}

static double clamp_axis(double center, int image_size, int viewport_size,
                         double zoom)
{
    double half;
    if (image_size <= 0 || viewport_size <= 0 || zoom <= 0.0)
        return image_size * 0.5;
    half = viewport_size * 0.5 / zoom;
    if (image_size * zoom <= viewport_size)
        return image_size * 0.5;
    return clamp_double(center, half, image_size - half);
}

void CmpView_ClampEdges(cmp_view_t *view, int image_w, int image_h,
                        int view_w, int view_h)
{
    if (!view)
        return;
    view->u = clamp_axis(view->u, image_w, view_w, view->zoom);
    view->v = clamp_axis(view->v, image_h, view_h, view->zoom);
}

void CmpView_ScreenToImage(const cmp_view_t *view, int view_w, int view_h,
                           double x, double y, double *image_x,
                           double *image_y)
{
    if (!view || !image_x || !image_y || view->zoom <= 0.0)
        return;
    *image_x = view->u + (x - view_w * 0.5) / view->zoom;
    *image_y = view->v + (y - view_h * 0.5) / view->zoom;
}

void Cmp_Blit(HDC dc, cmp_image_t *image, const cmp_view_t *view,
              const RECT *viewport, const RECT *clip)
{
    cmp_level_t level;
    BITMAPINFO bitmap_info;
    double zoom, origin_x, origin_y, visible_left, visible_top;
    double visible_right, visible_bottom, level_zoom;
    int k = 0, level_count, saved;
    int sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1;

    if (!dc || !image || !image->img.valid || !view ||
        !viewport || !clip || view->zoom <= 0.0)
        return;
    zoom = view->zoom;
    origin_x = viewport->left +
               (viewport->right - viewport->left) * 0.5 - view->u * zoom;
    origin_y = viewport->top +
               (viewport->bottom - viewport->top) * 0.5 - view->v * zoom;
    visible_left = dmax((double)clip->left, origin_x);
    visible_top = dmax((double)clip->top, origin_y);
    visible_right = dmin((double)clip->right,
                         origin_x + image->img.w * zoom);
    visible_bottom = dmin((double)clip->bottom,
                          origin_y + image->img.h * zoom);
    if (visible_right <= visible_left || visible_bottom <= visible_top)
        return;

    if (zoom < 1.0) {
        level_count = CmpImage_Levels(image);
        while (k + 1 < level_count &&
               zoom * (double)(1 << (k + 1)) <= 1.0)
            k++;
    }
    if (!CmpImage_Level(image, k, &level) &&
        !CmpImage_Level(image, 0, &level))
        return;
    level_zoom = zoom * (double)(1 << level.shift);
    sx0 = (int)floor((visible_left - origin_x) / level_zoom);
    sy0 = (int)floor((visible_top - origin_y) / level_zoom);
    sx1 = (int)ceil((visible_right - origin_x) / level_zoom);
    sy1 = (int)ceil((visible_bottom - origin_y) / level_zoom);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > level.w) sx1 = level.w;
    if (sy1 > level.h) sy1 = level.h;
    if (sx1 <= sx0 || sy1 <= sy0)
        return;

    dx0 = (int)floor(origin_x + sx0 * level_zoom + 0.5);
    dy0 = (int)floor(origin_y + sy0 * level_zoom + 0.5);
    dx1 = (int)floor(origin_x + sx1 * level_zoom + 0.5);
    dy1 = (int)floor(origin_y + sy1 * level_zoom + 0.5);
    if (dx1 <= dx0) dx1 = dx0 + 1;
    if (dy1 <= dy0) dy1 = dy0 + 1;

    saved = SaveDC(dc);
    if (saved == 0)
        return;
    IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
    if (level_zoom < 1.0) {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
    } else {
        SetStretchBltMode(dc, COLORONCOLOR);
    }
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = level.w;
    bitmap_info.bmiHeader.biHeight = -(sy1 - sy0);
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                  sx0, 0, sx1 - sx0, sy1 - sy0,
                  level.px + (size_t)sy0 * (size_t)level.pitch,
                  &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(dc, saved);
}
