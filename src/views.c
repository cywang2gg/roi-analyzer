#include "views.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "image_wic.h"
#include "report.h"

#define VIEW_WIDTH 256
#define VIEW_HEIGHT 192

static double luma(const BYTE *p)
{
    return 0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0];
}

static BYTE clamp_byte(double value)
{
    if (value < 0.0) return 0;
    if (value > 255.0) return 255;
    return (BYTE)(value + 0.5);
}

static BOOL encode_view(const image_t *img, const metrics_masks_t *masks,
                        int mode, metrics_stage2_t *stage2)
{
    BYTE *pixels;
    BYTE *png = NULL;
    size_t png_size = 0, encoded_size = 0;
    int x, y;
    pixels = (BYTE *)malloc((size_t)VIEW_WIDTH * VIEW_HEIGHT * 4);
    if (!pixels)
        return FALSE;
    for (y = 0; y < VIEW_HEIGHT; y++) {
        int sy = masks->rect.top +
            (int)((int64_t)y * masks->height / VIEW_HEIGHT);
        BYTE *dst = pixels + (size_t)y * VIEW_WIDTH * 4;
        for (x = 0; x < VIEW_WIDTH; x++) {
            int sx = masks->rect.left +
                (int)((int64_t)x * masks->width / VIEW_WIDTH);
            const BYTE *src = img->px + (size_t)sy * (size_t)img->pitch +
                              (size_t)sx * 4;
            double value;
            BYTE r, g, b;
            if (mode == 0) {
                BYTE klass = MetricsMasks_ClassAt(
                    masks, sx - masks->rect.left, sy - masks->rect.top);
                int tier = (klass & METRICS_MASK_TIER_MASK) >>
                           METRICS_MASK_TIER_SHIFT;
                if (klass & METRICS_MASK_EDGE) {
                    r = tier == 0 ? 40 : (tier == 1 ? 255 : 255);
                    g = tier == 0 ? 210 : (tier == 1 ? 190 : 60);
                    b = tier == 0 ? 255 : 40;
                } else {
                    r = g = b = (BYTE)(luma(src) * 0.28);
                }
            } else if (mode == 1) {
                int dx, dy;
                double mean = 0.0;
                for (dy = -1; dy <= 1; dy++) {
                    int py = sy + dy;
                    if (py < masks->rect.top) py = masks->rect.top;
                    if (py >= masks->rect.bottom) py = masks->rect.bottom - 1;
                    for (dx = -1; dx <= 1; dx++) {
                        int px = sx + dx;
                        const BYTE *p;
                        if (px < masks->rect.left) px = masks->rect.left;
                        if (px >= masks->rect.right) px = masks->rect.right - 1;
                        p = img->px + (size_t)py * (size_t)img->pitch +
                            (size_t)px * 4;
                        mean += luma(p) / 9.0;
                    }
                }
                value = fmin(255.0, fabs(luma(src) - mean) * 8.0);
                r = g = b = clamp_byte(value);
            } else {
                double original = luma(src);
                value = 255.0 * pow(original / 255.0, 0.62);
                r = g = b = clamp_byte(value);
            }
            dst[(size_t)x * 4] = b;
            dst[(size_t)x * 4 + 1] = g;
            dst[(size_t)x * 4 + 2] = r;
            dst[(size_t)x * 4 + 3] = 255;
        }
    }
    if (!Image_EncodePNGMemory(pixels, VIEW_WIDTH, VIEW_HEIGHT,
                               VIEW_WIDTH * 4, &png, &png_size)) {
        free(pixels);
        return FALSE;
    }
    free(pixels);
    stage2->v2_png_base64[mode] =
        Report_Base64Encode(png, png_size, &encoded_size);
    Image_FreePNGMemory(png);
    if (!stage2->v2_png_base64[mode])
        return FALSE;
    stage2->v2_png_base64_len[mode] = encoded_size;
    return TRUE;
}

BOOL MetricsViews_Build(const image_t *img, const metrics_masks_t *masks,
                        metrics_stage2_t *stage2)
{
    int i;
    if (!img || !masks || (!masks->classes && !masks->reference) || !stage2)
        return FALSE;
    for (i = 0; i < 3; i++) {
        if (!encode_view(img, masks, i, stage2)) {
            MetricsViews_Free(stage2);
            return FALSE;
        }
    }
    return TRUE;
}

void MetricsViews_Free(metrics_stage2_t *stage2)
{
    int i;
    if (!stage2)
        return;
    for (i = 0; i < 3; i++) {
        free(stage2->v2_png_base64[i]);
        stage2->v2_png_base64[i] = NULL;
        stage2->v2_png_base64_len[i] = 0;
    }
}
