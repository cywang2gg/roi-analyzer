#include "view.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"

void ViewPyr_Free(view_pyr_t *pyramid)
{
    int i;
    if (!pyramid)
        return;
    for (i = 0; i < pyramid->count; i++)
        if (pyramid->levels[i].owned)
            free(pyramid->levels[i].px);
    memset(pyramid, 0, sizeof(*pyramid));
}

BOOL ViewPyr_Build(view_pyr_t *pyramid, const image_t *img)
{
    int i;
    if (!pyramid || !img || !img->valid || !img->px ||
        img->w <= 0 || img->h <= 0 || img->pitch < img->w * 4)
        return FALSE;
    ViewPyr_Free(pyramid);
    pyramid->levels[0].px = img->px;
    pyramid->levels[0].w = img->w;
    pyramid->levels[0].h = img->h;
    pyramid->levels[0].pitch = img->pitch;
    pyramid->count = 1;
    for (i = 1; i < VIEW_MAX_LEVELS; i++) {
        const view_level_t *previous = &pyramid->levels[i - 1];
        view_level_t *level = &pyramid->levels[i];
        size_t bytes;
        int x, y;
        level->w = (previous->w + 1) / 2;
        level->h = (previous->h + 1) / 2;
        if (level->w == previous->w && level->h == previous->h)
            break;
        if ((size_t)level->w > SIZE_MAX / 4 ||
            (size_t)level->w * 4 > SIZE_MAX / (size_t)level->h) {
            ViewPyr_Free(pyramid);
            return FALSE;
        }
        level->pitch = level->w * 4;
        bytes = (size_t)level->pitch * (size_t)level->h;
        level->px = (unsigned char *)malloc(bytes);
        if (!level->px) {
            ViewPyr_Free(pyramid);
            return FALSE;
        }
        level->owned = TRUE;
        for (y = 0; y < level->h; y++) {
            unsigned char *destination = level->px + (size_t)y * level->pitch;
            int sy, sx;
            for (x = 0; x < level->w; x++) {
                unsigned int sum[4] = { 0, 0, 0, 0 };
                unsigned int samples = 0;
                int dy, dx;
                for (dy = 0; dy < 2; dy++) {
                    sy = y * 2 + dy;
                    if (sy >= previous->h)
                        continue;
                    for (dx = 0; dx < 2; dx++) {
                        const unsigned char *source;
                        sx = x * 2 + dx;
                        if (sx >= previous->w)
                            continue;
                        source = previous->px + (size_t)sy * previous->pitch +
                                 (size_t)sx * 4;
                        sum[0] += source[0];
                        sum[1] += source[1];
                        sum[2] += source[2];
                        sum[3] += source[3];
                        samples++;
                    }
                }
                destination[x * 4 + 0] =
                    (unsigned char)((sum[0] + samples / 2) / samples);
                destination[x * 4 + 1] =
                    (unsigned char)((sum[1] + samples / 2) / samples);
                destination[x * 4 + 2] =
                    (unsigned char)((sum[2] + samples / 2) / samples);
                destination[x * 4 + 3] =
                    (unsigned char)((sum[3] + samples / 2) / samples);
            }
        }
        pyramid->count++;
    }
    return TRUE;
}

const view_level_t *ViewPyr_Pick(const view_pyr_t *pyramid,
                                 int draw_w, int draw_h, BOOL nearest)
{
    int i, picked = 0;
    if (!pyramid || pyramid->count <= 0)
        return NULL;
    if (nearest)
        return &pyramid->levels[0];
    for (i = 1; i < pyramid->count; i++) {
        const view_level_t *level = &pyramid->levels[i];
        if (level->w >= draw_w && level->h >= draw_h)
            picked = i;
        else
            break;
    }
    return &pyramid->levels[picked];
}

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
    view_level_t level;
    view_pyr_t pyramid;
    if (!img || !img->valid || !img->px)
        return;
    memset(&level, 0, sizeof(level));
    level.px = img->px;
    level.w = img->w;
    level.h = img->h;
    level.pitch = img->pitch;
    memset(&pyramid, 0, sizeof(pyramid));
    pyramid.levels[0] = level;
    pyramid.count = 1;
    View_DrawImagePyramid(hdc, v, img, &pyramid);
}

void View_DrawImagePyramid(HDC hdc, const view_t *v, const image_t *img,
                           const view_pyr_t *pyramid)
{
    BITMAPINFO bmi;
    const view_level_t *level;
    RECT visible;
    BOOL nearest;

    if (!hdc || !v || !img || !img->valid || !img->px)
        return;
    if (v->draw_w <= 0 || v->draw_h <= 0)
        return;
    nearest = v->zoom > 1.0f;
    level = ViewPyr_Pick(pyramid, v->draw_w, v->draw_h, nearest);
    if (!level)
        return;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = level->w;
    bmi.bmiHeader.biHeight = -level->h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    visible.left = v->off_x;
    visible.top = v->off_y;
    visible.right = v->off_x + v->draw_w;
    visible.bottom = v->off_y + v->draw_h;
    if (GetClipBox(hdc, &visible) == ERROR)
        return;
    {
        RECT image_rect;
        image_rect.left = v->off_x;
        image_rect.top = v->off_y;
        image_rect.right = v->off_x + v->draw_w;
        image_rect.bottom = v->off_y + v->draw_h;
        if (!IntersectRect(&visible, &visible, &image_rect))
            return;
    }
    SetStretchBltMode(hdc, nearest ? COLORONCOLOR : HALFTONE);
    if (!nearest)
        SetBrushOrgEx(hdc, 0, 0, NULL);
    if (nearest) {
        int sx0 = (int)(((double)(visible.left - v->off_x) * level->w) /
                        v->draw_w);
        int sy0 = (int)(((double)(visible.top - v->off_y) * level->h) /
                        v->draw_h);
        int sx1 = (int)(((double)(visible.right - v->off_x) * level->w +
                         v->draw_w - 1) / v->draw_w);
        int sy1 = (int)(((double)(visible.bottom - v->off_y) * level->h +
                         v->draw_h - 1) / v->draw_h);
        if (sx1 > level->w) sx1 = level->w;
        if (sy1 > level->h) sy1 = level->h;
        StretchDIBits(hdc, visible.left, visible.top,
                      visible.right - visible.left,
                      visible.bottom - visible.top,
                      sx0, sy0, sx1 - sx0, sy1 - sy0,
                      level->px, &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        StretchDIBits(hdc, v->off_x, v->off_y, v->draw_w, v->draw_h,
                      0, 0, level->w, level->h,
                      level->px, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }
}
