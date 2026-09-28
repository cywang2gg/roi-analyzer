#include "masks.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"

#define MASK_MAX_PIXELS 5500000u
#define GRADIENT_BINS 4096

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static double pixel_y(const BYTE *pixel)
{
    return 0.299 * pixel[2] + 0.587 * pixel[1] + 0.114 * pixel[0];
}

static void smooth_row(const image_t *img, RECT rect, int y, double *out)
{
    static const int weights[3] = { 1, 2, 1 };
    int x, dx, dy;
    y = clamp_int(y, rect.top, rect.bottom - 1);
    for (x = 0; x < rect.right - rect.left; x++) {
        double sum = 0.0;
        for (dy = -1; dy <= 1; dy++) {
            int sy = clamp_int(y + dy, rect.top, rect.bottom - 1);
            const BYTE *row = img->px + (size_t)sy * (size_t)img->pitch;
            for (dx = -1; dx <= 1; dx++) {
                int sx = clamp_int(rect.left + x + dx,
                                   rect.left, rect.right - 1);
                sum += weights[dy + 1] * weights[dx + 1] *
                       pixel_y(row + (size_t)sx * 4);
            }
        }
        out[x] = sum / 16.0;
    }
}

static double sobel_at(const double *up, const double *center,
                       const double *down, int x, int width,
                       double *out_gx, double *out_gy)
{
    int left = x > 0 ? x - 1 : 0;
    int right = x + 1 < width ? x + 1 : width - 1;
    double gx = -up[left] + up[right] - 2.0 * center[left] +
                2.0 * center[right] - down[left] + down[right];
    double gy = -up[left] - 2.0 * up[x] - up[right] +
                down[left] + 2.0 * down[x] + down[right];
    if (out_gx) *out_gx = gx;
    if (out_gy) *out_gy = gy;
    return hypot(gx, gy);
}

static void gradient_row(const image_t *img, RECT rect, int y,
                         double *up, double *center, double *down,
                         double *output)
{
    int x, width = rect.right - rect.left;
    smooth_row(img, rect, y - 1, up);
    smooth_row(img, rect, y, center);
    smooth_row(img, rect, y + 1, down);
    for (x = 0; x < width; x++)
        output[x] = sobel_at(up, center, down, x, width, NULL, NULL);
}

static void sample_l(const image_t *img, RECT rect, double x, double y,
                     double *out)
{
    int ix = clamp_int((int)floor(x + 0.5), rect.left, rect.right - 1);
    int iy = clamp_int((int)floor(y + 0.5), rect.top, rect.bottom - 1);
    const BYTE *p = img->px + (size_t)iy * (size_t)img->pitch +
                    (size_t)ix * 4;
    float l, a, b;
    rgb_to_lab_f(p[2], p[1], p[0], &l, &a, &b);
    *out = l;
}

static void sort4(double values[4])
{
    int i, j;
    for (i = 1; i < 4; i++) {
        double value = values[i];
        for (j = i; j > 0 && values[j - 1] > value; j--)
            values[j] = values[j - 1];
        values[j] = value;
    }
}

static int contrast_tier(const image_t *img, RECT rect, int x, int y,
                         double gx, double gy)
{
    double norm = hypot(gx, gy), side_a[4], side_b[4], delta;
    int i;
    if (norm < 1e-9)
        return 0;
    for (i = 0; i < 4; i++) {
        double distance = (double)(i + 1);
        double ox = gx * distance / norm;
        double oy = gy * distance / norm;
        sample_l(img, rect, rect.left + x + ox, rect.top + y + oy,
                 &side_a[i]);
        sample_l(img, rect, rect.left + x - ox, rect.top + y - oy,
                 &side_b[i]);
    }
    sort4(side_a);
    sort4(side_b);
    delta = fabs((side_a[1] + side_a[2] - side_b[1] - side_b[2]) * 0.5);
    return delta < 10.0 ? 0 : (delta <= 30.0 ? 1 : 2);
}

static BOOL neutral_pixel(const image_t *img, RECT rect, int x, int y)
{
    int dx, dy;
    double r = 0.0, g = 0.0, b = 0.0;
    float l, a, lab_b;
    for (dy = -2; dy <= 2; dy++) {
        int sy = clamp_int(y + dy, rect.top, rect.bottom - 1);
        const BYTE *row = img->px + (size_t)sy * (size_t)img->pitch;
        for (dx = -2; dx <= 2; dx++) {
            int sx = clamp_int(x + dx, rect.left, rect.right - 1);
            const BYTE *pixel = row + (size_t)sx * 4;
            b += pixel[0]; g += pixel[1]; r += pixel[2];
        }
    }
    rgb_to_lab_f((float)(r / 25.0), (float)(g / 25.0), (float)(b / 25.0),
                 &l, &a, &lab_b);
    return hypot(a, lab_b) < 8.0 && l > 15.0f && l < 95.0f;
}

BOOL MetricsMasks_BuildCancelable(const image_t *img, RECT rect,
                                 volatile BOOL *cancel_requested,
                                 metrics_masks_t *masks)
{
    double *rows[4] = { NULL, NULL, NULL, NULL };
    uint8_t *edge_ring = NULL;
    int *vertical_edges = NULL;
    uint64_t histogram[GRADIENT_BINS] = { 0 };
    size_t pixels, i, edge_rank, flat_rank, seen;
    int width, height, x, y, slot;
    double edge_threshold = 0.0, flat_threshold = 0.0;
    BOOL have_edge_threshold = FALSE, have_flat_threshold = FALSE;
    if (!masks)
        return FALSE;
    memset(masks, 0, sizeof(*masks));
    if (!img || !img->valid || !img->px || rect.left < 0 || rect.top < 0 ||
        rect.right > img->w || rect.bottom > img->h ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return FALSE;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return FALSE;
    pixels = (size_t)width * (size_t)height;
    if (!pixels || pixels > MASK_MAX_PIXELS)
        return FALSE;
    masks->classes = (uint8_t *)calloc(pixels, 1);
    if (!masks->classes)
        return FALSE;
    masks->owns_classes = TRUE;
    for (slot = 0; slot < 4; slot++) {
        rows[slot] = (double *)malloc((size_t)width * sizeof(double));
        if (!rows[slot])
            goto fail;
    }

    /* A bounded integer histogram supplies deterministic percentile cutoffs. */
    for (y = 0; y < height; y++) {
        if (cancel_requested &&
            InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                       0, 0))
            goto fail;
        gradient_row(img, rect, rect.top + y, rows[0], rows[1], rows[2],
                     rows[3]);
        for (x = 0; x < width; x++) {
            int bin = (int)floor(rows[3][x]);
            if (bin >= GRADIENT_BINS) bin = GRADIENT_BINS - 1;
            histogram[bin]++;
        }
    }
    edge_rank = (pixels - 1) / 2;
    flat_rank = (pixels - 1) * 30 / 100;
    seen = 0;
    for (i = 0; i < GRADIENT_BINS; i++) {
        seen += (size_t)histogram[i];
        if (!have_edge_threshold && seen > edge_rank) {
            edge_threshold = (double)i;
            have_edge_threshold = TRUE;
        }
        if (!have_flat_threshold && seen > flat_rank) {
            flat_threshold = (double)i;
            have_flat_threshold = TRUE;
        }
        if (have_edge_threshold && have_flat_threshold)
            break;
    }

    for (y = 0; y < height; y++) {
        if (cancel_requested &&
            InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                       0, 0))
            goto fail;
        gradient_row(img, rect, rect.top + y, rows[0], rows[1], rows[2],
                     rows[3]);
        for (x = 0; x < width; x++) {
            size_t index = (size_t)y * (size_t)width + (size_t)x;
            double gx, gy;
            double magnitude = sobel_at(rows[0], rows[1], rows[2], x,
                                        width, &gx, &gy);
            BOOL interior = x >= 3 && y >= 3 &&
                            x + 3 < width && y + 3 < height;
            if (interior && magnitude > edge_threshold) {
                masks->classes[index] |= METRICS_MASK_EDGE;
                masks->classes[index] |=
                    (uint8_t)(contrast_tier(img, rect, x, y, gx, gy)
                              << METRICS_MASK_TIER_SHIFT);
            }
            if (neutral_pixel(img, rect, rect.left + x, rect.top + y))
                masks->classes[index] |= METRICS_MASK_NEUTRAL;
        }
    }

    edge_ring = (uint8_t *)calloc((size_t)11 * (size_t)width, 1);
    vertical_edges = (int *)calloc((size_t)width, sizeof(*vertical_edges));
    if (!edge_ring || !vertical_edges)
        goto fail;
    /* Sliding horizontal and vertical windows implement the 11x11 dilation. */
    for (y = 0; y < height; y++) {
        if (cancel_requested &&
            InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                       0, 0))
            goto fail;
        int slot_index = y % 11;
        uint8_t *slot_data = edge_ring + (size_t)slot_index * (size_t)width;
        int running = 0;
        for (x = 0; x < width; x++) {
            int remove_x = x - 6;
            int add_x = x + 5;
            if (remove_x >= 0 &&
                (masks->classes[(size_t)y * (size_t)width +
                                (size_t)remove_x] & METRICS_MASK_EDGE))
                running--;
            if (add_x < width &&
                (masks->classes[(size_t)y * (size_t)width +
                                (size_t)add_x] & METRICS_MASK_EDGE))
                running++;
            if (slot_data[x])
                vertical_edges[x]--;
            slot_data[x] = running > 0 ? 1 : 0;
            vertical_edges[x] += slot_data[x];
            if (y >= 10) {
                int target_y = y - 5;
                size_t target = (size_t)target_y * (size_t)width +
                                (size_t)x;
                if (x >= 5 && x + 5 < width && target_y >= 5 &&
                    target_y + 5 < height &&
                    vertical_edges[x] == 0)
                    masks->classes[target] |= METRICS_MASK_FLAT;
            }
        }
    }
    free(edge_ring);
    free(vertical_edges);
    edge_ring = NULL;
    vertical_edges = NULL;
    masks->rect = rect;
    masks->width = width;
    masks->height = height;
    masks->count = pixels;
    masks->edge_threshold = edge_threshold;
    masks->flat_threshold = flat_threshold;
    for (slot = 0; slot < 4; slot++)
        free(rows[slot]);
    return TRUE;

fail:
    free(edge_ring);
    free(vertical_edges);
    for (slot = 0; slot < 4; slot++)
        free(rows[slot]);
    MetricsMasks_Free(masks);
    return FALSE;
}

BOOL MetricsMasks_Build(const image_t *img, RECT rect,
                        metrics_masks_t *masks)
{
    return MetricsMasks_BuildCancelable(img, rect, NULL, masks);
}

BOOL MetricsMasks_UseReference(const metrics_masks_t *reference, RECT rect,
                               metrics_masks_t *mapped)
{
    size_t count;
    int width, height;
    if (!reference || !reference->classes || !mapped ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return FALSE;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return FALSE;
    count = (size_t)width * (size_t)height;
    memset(mapped, 0, sizeof(*mapped));
    mapped->rect = rect;
    mapped->width = width;
    mapped->height = height;
    mapped->count = count;
    mapped->reference = reference;
    mapped->edge_threshold = reference->edge_threshold;
    mapped->flat_threshold = reference->flat_threshold;
    return TRUE;
}

uint8_t MetricsMasks_ClassAt(const metrics_masks_t *masks, int x, int y)
{
    if (!masks || x < 0 || y < 0 || x >= masks->width || y >= masks->height)
        return 0;
    if (masks->reference) {
        int sx = (int)((int64_t)x * masks->reference->width / masks->width);
        int sy = (int)((int64_t)y * masks->reference->height / masks->height);
        return MetricsMasks_ClassAt(masks->reference, sx, sy);
    }
    return masks->classes ?
        masks->classes[(size_t)y * (size_t)masks->width + (size_t)x] : 0;
}

void MetricsMasks_Free(metrics_masks_t *masks)
{
    if (!masks)
        return;
    if (masks->owns_classes)
        free(masks->classes);
    memset(masks, 0, sizeof(*masks));
}
