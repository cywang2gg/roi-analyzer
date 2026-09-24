#include "analyze.h"

#include <math.h>
#include <string.h>

#include "image.h"

void rgb_to_lab_f(float r, float g, float b, float *l, float *a, float *bl)
{
    float R = r / 255.0f, G = g / 255.0f, B = b / 255.0f;
    float X, Y, Z;

    /* Ported as-is from c-vlcplayer src/analysis.c rgb_to_lab (D65, sRGB). */
    R = (R > 0.04045f) ? powf((R + 0.055f) / 1.055f, 2.4f) : R / 12.92f;
    G = (G > 0.04045f) ? powf((G + 0.055f) / 1.055f, 2.4f) : G / 12.92f;
    B = (B > 0.04045f) ? powf((B + 0.055f) / 1.055f, 2.4f) : B / 12.92f;
    X = R * 0.4124f + G * 0.3576f + B * 0.1805f;
    Y = R * 0.2126f + G * 0.7152f + B * 0.0722f;
    Z = R * 0.0193f + G * 0.1192f + B * 0.9505f;
    X /= 0.95047f;
    Y /= 1.00000f;
    Z /= 1.08883f;
    X = (X > 0.008856f) ? powf(X, 1.0f / 3.0f) : (7.787f * X) + (16.0f / 116.0f);
    Y = (Y > 0.008856f) ? powf(Y, 1.0f / 3.0f) : (7.787f * Y) + (16.0f / 116.0f);
    Z = (Z > 0.008856f) ? powf(Z, 1.0f / 3.0f) : (7.787f * Z) + (16.0f / 116.0f);
    *l = (116.0f * Y) - 16.0f;
    *a = 500.0f * (X - Y);
    *bl = 200.0f * (Y - Z);
}

void AnalyzeROI(const image_t *img, RECT rc, roi_result_t *out)
{
    int x0, y0, x1, y1, t;
    int x, y, n = 0;
    double sr = 0.0, sg = 0.0, sb = 0.0, sy = 0.0;
    double sr2 = 0.0, sg2 = 0.0, sb2 = 0.0, sy2 = 0.0;
    double var;
    float fl, fa, fb;

    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!img || !img->valid || !img->px)
        return;

    /* Order + clamp to [0, w-1] x [0, h-1]. */
    x0 = rc.left;
    x1 = rc.right;
    y0 = rc.top;
    y1 = rc.bottom;
    if (x0 > x1) {
        t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        t = y0;
        y0 = y1;
        y1 = t;
    }
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= img->w)
        x1 = img->w - 1;
    if (y1 >= img->h)
        y1 = img->h - 1;
    if (x1 < x0 || y1 < y0)
        return;

    for (y = y0; y <= y1; y++) {
        const unsigned char *row = img->px + (size_t)y * (size_t)img->pitch;
        for (x = x0; x <= x1; x++) {
            /* px layout is BGRA. */
            double b = (double)row[x * 4 + 0];
            double g = (double)row[x * 4 + 1];
            double r = (double)row[x * 4 + 2];
            double yy = 0.299 * r + 0.587 * g + 0.114 * b;
            sr += r;
            sg += g;
            sb += b;
            sy += yy;
            sr2 += r * r;
            sg2 += g * g;
            sb2 += b * b;
            sy2 += yy * yy;
            n++;
        }
    }
    if (n <= 0)
        return;

    out->x0 = x0;
    out->y0 = y0;
    out->x1 = x1;
    out->y1 = y1;
    out->count = n;
    out->r_mean = sr / n;
    out->g_mean = sg / n;
    out->b_mean = sb / n;
    out->y_mean = sy / n;

    /* Population std: sqrt(E[x^2] - mean^2), guarded against round-off. */
    var = sr2 / n - out->r_mean * out->r_mean;
    out->r_std = sqrt(var > 0.0 ? var : 0.0);
    var = sg2 / n - out->g_mean * out->g_mean;
    out->g_std = sqrt(var > 0.0 ? var : 0.0);
    var = sb2 / n - out->b_mean * out->b_mean;
    out->b_std = sqrt(var > 0.0 ? var : 0.0);
    var = sy2 / n - out->y_mean * out->y_mean;
    out->y_std = sqrt(var > 0.0 ? var : 0.0);

    rgb_to_lab_f((float)out->r_mean, (float)out->g_mean, (float)out->b_mean, &fl, &fa, &fb);
    out->lab_l = (double)fl;
    out->lab_a = (double)fa;
    out->lab_b = (double)fb;
}
