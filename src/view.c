#include "view.h"

#include <string.h>

#include "image.h"

void View_Update(view_t *v, int cw, int ch, int iw, int ih)
{
    float sx, sy;

    if (!v)
        return;
    if (cw <= 0 || ch <= 0 || iw <= 0 || ih <= 0) {
        v->off_x = v->off_y = 0;
        v->draw_w = v->draw_h = 0;
        v->scale = 1.0f;
        return;
    }
    sx = (float)cw / (float)iw;
    sy = (float)ch / (float)ih;
    v->scale = (sx < sy) ? sx : sy;
    v->draw_w = (int)((float)iw * v->scale);
    v->draw_h = (int)((float)ih * v->scale);
    if (v->draw_w < 1)
        v->draw_w = 1;
    if (v->draw_h < 1)
        v->draw_h = 1;
    v->off_x = (cw - v->draw_w) / 2;
    v->off_y = (ch - v->draw_h) / 2;
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

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, v->off_x, v->off_y, v->draw_w, v->draw_h,
                  0, 0, img->w, img->h,
                  img->px, &bmi, DIB_RGB_COLORS, SRCCOPY);
}
