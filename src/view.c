#include "view.h"

#include <limits.h>
#include <string.h>

#include "image.h"

static int add_saturated(int value, int delta)
{
    long long sum = (long long)value + (long long)delta;
    if (sum < INT_MIN)
        return INT_MIN;
    if (sum > INT_MAX)
        return INT_MAX;
    return (int)sum;
}

static int clamped_offset(int centered, int pan, int min_offset,
                          int max_offset)
{
    long long offset = (long long)centered + (long long)pan;
    if (offset < min_offset)
        return min_offset;
    if (offset > max_offset)
        return max_offset;
    return (int)offset;
}

static float clamp_zoom(float zoom)
{
    if (zoom < 0.1f)
        return 0.1f;
    if (zoom > 8.0f)
        return 8.0f;
    return zoom;
}

void View_Update(view_t *v, int cw, int ch, int iw, int ih, float zoom)
{
    float sx, sy;

    if (!v)
        return;
    v->zoom = clamp_zoom(zoom);
    if (cw <= 0 || ch <= 0 || iw <= 0 || ih <= 0) {
        v->off_x = v->off_y = 0;
        v->draw_w = v->draw_h = 0;
        v->scale = 1.0f;
        return;
    }
    sx = (float)cw / (float)iw;
    sy = (float)ch / (float)ih;
    v->scale = ((sx < sy) ? sx : sy) * v->zoom;
    v->draw_w = (int)((float)iw * v->scale);
    v->draw_h = (int)((float)ih * v->scale);
    if (v->draw_w < 1)
        v->draw_w = 1;
    if (v->draw_h < 1)
        v->draw_h = 1;
    {
        int centered_x = (cw - v->draw_w) / 2;
        int centered_y = (ch - v->draw_h) / 2;
        if (v->draw_w <= cw)
            v->off_x = centered_x;
        else
            v->off_x = clamped_offset(centered_x, v->pan_x,
                                      cw - v->draw_w, 0);
        if (v->draw_h <= ch)
            v->off_y = centered_y;
        else
            v->off_y = clamped_offset(centered_y, v->pan_y,
                                      ch - v->draw_h, 0);
    }
}

void View_Pan(view_t *v, const image_t *img, int cw, int ch, int dx, int dy)
{
    if (!v)
        return;
    v->pan_x = add_saturated(v->pan_x, dx);
    v->pan_y = add_saturated(v->pan_y, dy);
    View_Update(v, cw, ch,
                img && img->valid ? img->w : 0,
                img && img->valid ? img->h : 0, v->zoom);
}

void View_Reset(view_t *v, const image_t *img, int cw, int ch)
{
    if (!v)
        return;
    v->pan_x = 0;
    v->pan_y = 0;
    View_Update(v, cw, ch,
                img && img->valid ? img->w : 0,
                img && img->valid ? img->h : 0, 1.0f);
}

void View_SetZoom(view_t *v, const image_t *img, int cw, int ch,
                  float new_zoom, POINT anchor)
{
    float image_x, image_y;
    float old_scale;
    int iw, ih;

    if (!v || !img || !img->valid || img->w <= 0 || img->h <= 0)
        return;
    old_scale = v->scale;
    if (old_scale <= 0.0f)
        return;
    image_x = (float)(anchor.x - v->off_x) / old_scale;
    image_y = (float)(anchor.y - v->off_y) / old_scale;
    iw = img->w;
    ih = img->h;
    View_Update(v, cw, ch, iw, ih, new_zoom);
    v->pan_x = anchor.x - (int)(image_x * v->scale) -
               (cw - v->draw_w) / 2;
    v->pan_y = anchor.y - (int)(image_y * v->scale) -
               (ch - v->draw_h) / 2;
    View_Update(v, cw, ch, iw, ih, v->zoom);
}

BOOL View_ToImage(const view_t *v, int iw, int ih, POINT wp, POINT *ip)
{
    int lx, ly;

    if (!v || !ip || iw <= 0 || ih <= 0 || v->scale <= 0.0f)
        return FALSE;
    lx = wp.x - v->off_x;
    ly = wp.y - v->off_y;
    if (lx < 0 || ly < 0 || lx >= v->draw_w || ly >= v->draw_h)
        return FALSE;
    ip->x = (int)((float)lx / v->scale);
    ip->y = (int)((float)ly / v->scale);
    if (ip->x < 0)
        ip->x = 0;
    if (ip->y < 0)
        ip->y = 0;
    if (ip->x >= iw)
        ip->x = iw - 1;
    if (ip->y >= ih)
        ip->y = ih - 1;
    return TRUE;
}

void View_ToWindow(const view_t *v, POINT ip, POINT *wp)
{
    if (!v || !wp)
        return;
    wp->x = v->off_x + (int)((float)ip.x * v->scale);
    wp->y = v->off_y + (int)((float)ip.y * v->scale);
}

void View_RectToWindow(const view_t *v, RECT image_rect, RECT *window_rect)
{
    if (!v || !window_rect)
        return;
    window_rect->left = v->off_x + (int)((float)image_rect.left * v->scale);
    window_rect->top = v->off_y + (int)((float)image_rect.top * v->scale);
    window_rect->right = v->off_x + (int)((float)(image_rect.right + 1) * v->scale);
    window_rect->bottom = v->off_y + (int)((float)(image_rect.bottom + 1) * v->scale);
}

void View_DrawImage(HDC hdc, const view_t *v, const image_t *img)
{
    BITMAPINFO bmi;

    if (!hdc || !v || !img || !img->valid || !img->px)
        return;
    if (v->draw_w <= 0 || v->draw_h <= 0)
        return;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img->w;
    bmi.bmiHeader.biHeight = -img->h; /* top-down: px[0] is the top row */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchDIBits(hdc, v->off_x, v->off_y, v->draw_w, v->draw_h,
                  0, 0, img->w, img->h,
                  img->px, &bmi, DIB_RGB_COLORS, SRCCOPY);
}
