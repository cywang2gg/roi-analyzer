#include "rotate.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static BOOL image_layout_valid(const image_t *img)
{
    return img && img->valid && img->px && img->w > 0 && img->h > 0 &&
           img->w <= INT_MAX / 4 && img->pitch >= img->w * 4 &&
           (size_t)img->pitch <= SIZE_MAX / (size_t)img->h;
}

static BOOL rotate_orthogonal(image_t *img, int degrees)
{
    unsigned char *pixels;
    int new_w, new_h, y, x;
    size_t bytes;

    if (!image_layout_valid(img))
        return FALSE;
    new_w = degrees == 180 ? img->w : img->h;
    new_h = degrees == 180 ? img->h : img->w;
    if (new_w > INT_MAX / 4 ||
        (size_t)(new_w * 4) > SIZE_MAX / (size_t)new_h)
        return FALSE;
    bytes = (size_t)new_w * 4 * (size_t)new_h;
    pixels = (unsigned char *)malloc(bytes);
    if (!pixels)
        return FALSE;

    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            int nx, ny;
            const unsigned char *src = img->px + (size_t)y * img->pitch +
                                       (size_t)x * 4;
            unsigned char *dst;
            if (degrees == 90) {
                nx = img->h - 1 - y;
                ny = x;
            } else if (degrees == 180) {
                nx = img->w - 1 - x;
                ny = img->h - 1 - y;
            } else {
                nx = y;
                ny = img->w - 1 - x;
            }
            dst = pixels + (size_t)ny * (size_t)new_w * 4 + (size_t)nx * 4;
            memcpy(dst, src, 4);
        }
    }
    free(img->px);
    img->px = pixels;
    img->w = new_w;
    img->h = new_h;
    img->pitch = new_w * 4;
    return TRUE;
}

BOOL Image_Rotate90(image_t *img)
{
    return rotate_orthogonal(img, 90);
}

BOOL Image_Rotate180(image_t *img)
{
    return rotate_orthogonal(img, 180);
}

BOOL Image_Rotate270(image_t *img)
{
    return rotate_orthogonal(img, 270);
}

static void sample_pixel(const image_t *img, double x, double y,
                         unsigned char pixel[4])
{
    int x0 = (int)floor(x), y0 = (int)floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    double fx = x - x0, fy = y - y0;
    int sx[4] = { x0, x1, x0, x1 };
    int sy[4] = { y0, y0, y1, y1 };
    double weights[4] = {
        (1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
        (1.0 - fx) * fy, fx * fy
    };
    int channel, i;

    for (channel = 0; channel < 4; channel++) {
        double value = 0.0;
        for (i = 0; i < 4; i++)
            if (sx[i] >= 0 && sx[i] < img->w &&
                sy[i] >= 0 && sy[i] < img->h)
                value += img->px[(size_t)sy[i] * (size_t)img->pitch +
                                 (size_t)sx[i] * 4 + (size_t)channel] *
                         weights[i];
        if (value < 0.0)
            value = 0.0;
        if (value > 255.0)
            value = 255.0;
        pixel[channel] = (unsigned char)(value + 0.5);
    }
}

BOOL Image_RotateArbitrary(image_t *img, double angle_deg)
{
    unsigned char *pixels;
    double radians, cosine, sine, out_w_exact, out_h_exact;
    double source_cx, source_cy, target_cx, target_cy;
    int new_w, new_h, x, y;
    size_t bytes;

    if (!image_layout_valid(img) || !isfinite(angle_deg))
        return FALSE;
    radians = angle_deg * (3.14159265358979323846 / 180.0);
    cosine = cos(radians);
    sine = sin(radians);
    out_w_exact = fabs((double)img->w * cosine) +
                  fabs((double)img->h * sine);
    out_h_exact = fabs((double)img->w * sine) +
                  fabs((double)img->h * cosine);
    if (!isfinite(out_w_exact) || !isfinite(out_h_exact) ||
        out_w_exact < 1.0 || out_h_exact < 1.0 ||
        out_w_exact > INT_MAX / 4 || out_h_exact > INT_MAX)
        return FALSE;
    new_w = (int)ceil(out_w_exact - 1e-10);
    new_h = (int)ceil(out_h_exact - 1e-10);
    if (new_w <= 0 || new_h <= 0 || new_w > INT_MAX / 4 ||
        (size_t)(new_w * 4) > SIZE_MAX / (size_t)new_h)
        return FALSE;
    bytes = (size_t)new_w * 4 * (size_t)new_h;
    pixels = (unsigned char *)malloc(bytes);
    if (!pixels)
        return FALSE;

    source_cx = ((double)img->w - 1.0) / 2.0;
    source_cy = ((double)img->h - 1.0) / 2.0;
    target_cx = ((double)new_w - 1.0) / 2.0;
    target_cy = ((double)new_h - 1.0) / 2.0;
    for (y = 0; y < new_h; y++) {
        for (x = 0; x < new_w; x++) {
            double dx = x - target_cx, dy = y - target_cy;
            double source_x = cosine * dx + sine * dy + source_cx;
            double source_y = -sine * dx + cosine * dy + source_cy;
            unsigned char *dst = pixels + (size_t)y * (size_t)new_w * 4 +
                                 (size_t)x * 4;
            sample_pixel(img, source_x, source_y, dst);
        }
    }
    free(img->px);
    img->px = pixels;
    img->w = new_w;
    img->h = new_h;
    img->pitch = new_w * 4;
    return TRUE;
}

static void normalize_rect(RECT *rc)
{
    int value;
    if (rc->left > rc->right) {
        value = rc->left;
        rc->left = rc->right;
        rc->right = value;
    }
    if (rc->top > rc->bottom) {
        value = rc->top;
        rc->top = rc->bottom;
        rc->bottom = value;
    }
}

void ROI_RotateRect90(RECT *rc, int old_w, int old_h)
{
    RECT result;
    if (!rc || old_w <= 0 || old_h <= 0)
        return;
    result.left = old_h - 1 - rc->bottom;
    result.right = old_h - 1 - rc->top;
    result.top = rc->left;
    result.bottom = rc->right;
    *rc = result;
    normalize_rect(rc);
}

void ROI_RotateRect180(RECT *rc, int old_w, int old_h)
{
    RECT result;
    if (!rc || old_w <= 0 || old_h <= 0)
        return;
    result.left = old_w - 1 - rc->right;
    result.right = old_w - 1 - rc->left;
    result.top = old_h - 1 - rc->bottom;
    result.bottom = old_h - 1 - rc->top;
    *rc = result;
    normalize_rect(rc);
}

void ROI_RotateRect270(RECT *rc, int old_w, int old_h)
{
    RECT result;
    if (!rc || old_w <= 0 || old_h <= 0)
        return;
    result.left = rc->top;
    result.right = rc->bottom;
    result.top = old_w - 1 - rc->right;
    result.bottom = old_w - 1 - rc->left;
    *rc = result;
    normalize_rect(rc);
}
