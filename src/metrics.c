#include "metrics.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "image_wic.h"
#include "report.h"

#define EDGE_PREVIEW_SIZE 512
#define NOISE_THRESHOLD 5.0

static BOOL is_cancel_requested(volatile BOOL *cancel_requested)
{
    return cancel_requested &&
           InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                     0, 0) != 0;
}

static BOOL valid_rect(const image_t *img, RECT input, RECT *output)
{
    LONG t;
    if (!img || !img->valid || !img->px || img->w <= 0 || img->h <= 0 ||
        img->w > INT_MAX / 4 || img->pitch < img->w * 4 || !output)
        return FALSE;
    if (input.left > input.right) {
        t = input.left;
        input.left = input.right;
        input.right = t;
    }
    if (input.top > input.bottom) {
        t = input.top;
        input.top = input.bottom;
        input.bottom = t;
    }
    if (input.left < 0) input.left = 0;
    if (input.top < 0) input.top = 0;
    if (input.right > img->w) input.right = img->w;
    if (input.bottom > img->h) input.bottom = img->h;
    if (input.right <= input.left || input.bottom <= input.top)
        return FALSE;
    *output = input;
    return TRUE;
}

static double pixel_y(const BYTE *pixel)
{
    return 0.299 * pixel[2] + 0.587 * pixel[1] + 0.114 * pixel[0];
}

static void fill_y_row(const image_t *img, int y, int x0, int width,
                       double *output)
{
    const BYTE *row = img->px + (size_t)y * (size_t)img->pitch +
                      (size_t)x0 * 4;
    int x;
    for (x = 0; x < width; x++)
        output[x] = pixel_y(row + (size_t)x * 4);
}

static double row_value(const double *row, int x, int width)
{
    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    return row[x];
}

BOOL Metrics_InitWorkspace(metrics_workspace_t *ws, int max_w)
{
    int i;
    if (!ws || max_w <= 0 ||
        (size_t)max_w > SIZE_MAX / sizeof(double))
        return FALSE;
    memset(ws, 0, sizeof(*ws));
    for (i = 0; i < 3; i++) {
        ws->row_buf_y[i] = (double *)malloc((size_t)max_w * sizeof(double));
        if (!ws->row_buf_y[i]) {
            Metrics_FreeWorkspace(ws);
            return FALSE;
        }
    }
    ws->buf_w = max_w;
    return TRUE;
}

void Metrics_FreeWorkspace(metrics_workspace_t *ws)
{
    int i;
    if (!ws)
        return;
    for (i = 0; i < 3; i++) {
        free(ws->row_buf_y[i]);
        ws->row_buf_y[i] = NULL;
    }
    free(ws->fft_buf);
    ws->fft_buf = NULL;
    ws->buf_w = 0;
}

static BOOL compute_spatial(const image_t *img, RECT rc,
                            metrics_workspace_t *ws,
                            metrics_stage1_t *out,
                            volatile BOOL *cancel_requested)
{
    RECT roi;
    int width, height, y, x;
    double n, lap_sum = 0.0, lap_sum_sq = 0.0;
    double sobel_sum = 0.0, tenengrad_sum = 0.0, brenner_sum = 0.0;
    double edge_count = 0.0, direction_sum = 0.0;
    double contrast_sq = 0.0, noise_sq = 0.0, noise_count = 0.0;
    double sat_sum = 0.0, sat_sum_sq = 0.0;
    double sat_low = 0.0, sat_high = 0.0;
    double zone_energy[METRICS_ZONE_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
    double zone_count[METRICS_ZONE_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
    double zone_min = 0.0, zone_max = 0.0;
    BOOL have_zone = FALSE;
    if (!out)
        return FALSE;
    memset(out, 0, sizeof(*out));
    if (!valid_rect(img, rc, &roi) || !ws)
        return FALSE;
    width = roi.right - roi.left;
    height = roi.bottom - roi.top;
    if (width > ws->buf_w)
        return FALSE;
    n = (double)width * (double)height;

    for (y = 0; y < height; y++) {
        int y_up = y > 0 ? y - 1 : 0;
        int y_down = y + 1 < height ? y + 1 : height - 1;
        const BYTE *row;
        int slot = y % 3;
        double *up = ws->row_buf_y[(slot + 2) % 3];
        double *center = ws->row_buf_y[slot];
        double *down = ws->row_buf_y[(slot + 1) % 3];
        if (is_cancel_requested(cancel_requested))
            return FALSE;
        fill_y_row(img, roi.top + y_up, roi.left, width, up);
        fill_y_row(img, roi.top + y, roi.left, width, center);
        fill_y_row(img, roi.top + y_down, roi.left, width, down);
        row = img->px + (size_t)(roi.top + y) * (size_t)img->pitch +
              (size_t)roi.left * 4;
        for (x = 0; x < width; x++) {
            double c = center[x];
            double left = row_value(center, x - 1, width);
            double right = row_value(center, x + 1, width);
            double up_left = row_value(up, x - 1, width);
            double up_mid = up[x];
            double up_right = row_value(up, x + 1, width);
            double down_left = row_value(down, x - 1, width);
            double down_mid = down[x];
            double down_right = row_value(down, x + 1, width);
            double lap = up_mid + left + right + down_mid - 4.0 * c;
            double gx = -up_left + up_right - 2.0 * left + 2.0 * right -
                        down_left + down_right;
            double gy = -up_left - 2.0 * up_mid - up_right + down_left +
                        2.0 * down_mid + down_right;
            double gradient_energy = gx * gx + gy * gy;
            double gradient = fabs(gx) + fabs(gy);
            double max_difference = 0.0;
            double differences[8] = {
                fabs(c - up_mid), fabs(c - up_right), fabs(c - right),
                fabs(c - down_right), fabs(c - down_mid),
                fabs(c - down_left), fabs(c - left), fabs(c - up_left)
            };
            int direction, zone;
            double b = row[(size_t)x * 4];
            double g = row[(size_t)x * 4 + 1];
            double r = row[(size_t)x * 4 + 2];
            double maximum = fmax(r, fmax(g, b));
            double minimum = fmin(r, fmin(g, b));
            double saturation = maximum > 0.0 ?
                               (maximum - minimum) / maximum : 0.0;
            lap_sum += lap;
            lap_sum_sq += lap * lap;
            sobel_sum += gradient;
            tenengrad_sum += gradient_energy;
            if (gradient >= 50.0)
                edge_count += 1.0;
            for (direction = 0; direction < 8; direction++)
                if (differences[direction] > max_difference)
                    max_difference = differences[direction];
            direction_sum += max_difference;
            if (x + 1 < width) {
                double delta = fabs(right - c);
                if (delta <= NOISE_THRESHOLD) {
                    noise_sq += delta * delta;
                    noise_count += 1.0;
                } else {
                    contrast_sq += delta * delta;
                }
            }
            if (x + 2 < width) {
                double delta = center[x + 2] - c;
                brenner_sum += delta * delta;
            }
            if (c <= 39.0)
                zone = METRICS_ZONE_SHADOW;
            else if (c <= 79.0)
                zone = METRICS_ZONE_LOW;
            else if (c <= 199.0)
                zone = METRICS_ZONE_MID;
            else
                zone = METRICS_ZONE_HIGH;
            zone_energy[zone] += gradient_energy;
            zone_count[zone] += 1.0;
            sat_sum += saturation;
            sat_sum_sq += saturation * saturation;
            if (saturation < 0.15)
                sat_low += 1.0;
            if (saturation > 0.85)
                sat_high += 1.0;
        }
    }
    out->laplacian_var = fmax(0.0, lap_sum_sq / n -
                                  (lap_sum / n) * (lap_sum / n));
    out->sobel_mean = sobel_sum / n;
    out->tenengrad = tenengrad_sum / n;
    out->brenner = brenner_sum / n;
    out->edge_density = edge_count * 100.0 / n;
    out->multi_dir_contrast = direction_sum / n;
    out->cumulative_contrast = contrast_sq;
    out->noise_estimate = noise_count > 0.0 ? sqrt(noise_sq / noise_count) : 0.0;
    out->snr_db = 10.0 * log10((contrast_sq + 1e-7) / (noise_sq + 1e-7));
    out->sat_mean = sat_sum / n;
    out->sat_std = sqrt(fmax(0.0, sat_sum_sq / n - out->sat_mean * out->sat_mean));
    out->sat_low_ratio = sat_low * 100.0 / n;
    out->sat_high_ratio = sat_high * 100.0 / n;
    for (x = 0; x < METRICS_ZONE_COUNT; x++) {
        double average = zone_count[x] > 0.0 ?
                         zone_energy[x] / zone_count[x] : 0.0;
        out->zone_contrast[x] = average;
        if (zone_count[x] > 0.0) {
            if (!have_zone) {
                zone_min = zone_max = average;
                have_zone = TRUE;
            } else {
                if (average < zone_min) zone_min = average;
                if (average > zone_max) zone_max = average;
            }
        }
    }
    out->zone_mean_range = have_zone ? zone_max - zone_min : 0.0;
    {
        double sharpness = sqrt(out->laplacian_var) +
                           0.25 * sqrt(out->tenengrad);
        out->quality_score = sharpness > 0.0 ?
                             100.0 * sharpness / (sharpness + 40.0) : 0.0;
    }
    return TRUE;
}

BOOL Metrics_ComputeSpatial(const image_t *img, RECT rc,
                            metrics_workspace_t *ws,
                            metrics_stage1_t *out_s1)
{
    return compute_spatial(img, rc, ws, out_s1, NULL);
}

static double roi_y(const image_t *img, int x, int y, RECT roi)
{
    if (x < roi.left) x = roi.left;
    if (x >= roi.right) x = roi.right - 1;
    if (y < roi.top) y = roi.top;
    if (y >= roi.bottom) y = roi.bottom - 1;
    return pixel_y(img->px + (size_t)y * (size_t)img->pitch + (size_t)x * 4);
}

static void set_cast_name(double a, double b, double delta, char *name,
                          size_t capacity)
{
    const char *value = "None";
    if (delta > 1.8) {
        if (fabs(b) >= fabs(a))
            value = b > 1.8 ? "Yellow" : (b < -1.8 ? "Blue" : "None");
        else
            value = a > 1.8 ? "Magenta" : (a < -1.8 ? "Green" : "None");
    }
    lstrcpynA(name, value, (int)capacity);
}

static BOOL compute_frequency_and_color(const image_t *img, RECT rc,
                                        metrics_workspace_t *ws,
                                        metrics_stage2_t *out_s2,
                                        volatile BOOL *cancel_requested)
{
    RECT roi;
    int width, height, size = 0, x, y;
    double *gray = NULL;
    double gray_a = 0.0, gray_b = 0.0, gray_n = 0.0;
    double shadow_a = 0.0, shadow_b = 0.0, shadow_n = 0.0;
    double rgb_r = 0.0, rgb_g = 0.0, rgb_b = 0.0, pixel_count = 0.0;
    if (!out_s2)
        return FALSE;
    memset(out_s2, 0, sizeof(*out_s2));
    if (!valid_rect(img, rc, &roi) || !ws)
        return FALSE;
    width = roi.right - roi.left;
    height = roi.bottom - roi.top;
    if (width >= FFT_MAX_SIZE && height >= FFT_MAX_SIZE)
        size = FFT_MAX_SIZE;
    else if (width >= 256 && height >= 256)
        size = 256;
    else if (width >= 128 && height >= 128)
        size = 128;

    for (y = roi.top; y < roi.bottom; y++) {
        const BYTE *row = img->px + (size_t)y * (size_t)img->pitch +
                          (size_t)roi.left * 4;
        if (is_cancel_requested(cancel_requested))
            return FALSE;
        for (x = roi.left; x < roi.right; x++) {
            const BYTE *pixel = row + (size_t)(x - roi.left) * 4;
            double r = pixel[2], g = pixel[1], b = pixel[0];
            double maximum = fmax(r, fmax(g, b));
            double minimum = fmin(r, fmin(g, b));
            double saturation = maximum > 0.0 ?
                                (maximum - minimum) / maximum : 0.0;
            double Y = pixel_y(pixel);
            float lab_l = 0.0f, lab_a = 0.0f, lab_b = 0.0f;
            BOOL need_lab = saturation < 0.12 || Y < 40.0;
            rgb_r += r;
            rgb_g += g;
            rgb_b += b;
            pixel_count += 1.0;
            if (need_lab)
                rgb_to_lab_f((float)r, (float)g, (float)b,
                             &lab_l, &lab_a, &lab_b);
            if (saturation < 0.12) {
                gray_a += lab_a;
                gray_b += lab_b;
                gray_n += 1.0;
            }
            if (Y < 40.0) {
                shadow_a += lab_a;
                shadow_b += lab_b;
                shadow_n += 1.0;
            }
        }
    }
    if (gray_n > 0.0) {
        out_s2->gray_cast_lab_a = gray_a / gray_n;
        out_s2->gray_cast_lab_b = gray_b / gray_n;
        out_s2->gray_cast_delta_e =
            sqrt(out_s2->gray_cast_lab_a * out_s2->gray_cast_lab_a +
                 out_s2->gray_cast_lab_b * out_s2->gray_cast_lab_b);
    }
    set_cast_name(out_s2->gray_cast_lab_a, out_s2->gray_cast_lab_b,
                  out_s2->gray_cast_delta_e, out_s2->gray_cast_text,
                  sizeof(out_s2->gray_cast_text));
    if (shadow_n > 0.0) {
        out_s2->shadow_lab_a = shadow_a / shadow_n;
        out_s2->shadow_lab_b = shadow_b / shadow_n;
        lstrcpynA(out_s2->shadow_defect,
                out_s2->shadow_lab_a > 3.0 && out_s2->shadow_lab_b < -2.0 ?
                "Purple" : "Normal", (int)sizeof(out_s2->shadow_defect));
    } else {
        lstrcpynA(out_s2->shadow_defect, "N/A",
                (int)sizeof(out_s2->shadow_defect));
    }
    if (pixel_count > 0.0) {
        float lab_l, lab_a, lab_b;
        rgb_to_lab_f((float)(rgb_r / pixel_count),
                     (float)(rgb_g / pixel_count),
                     (float)(rgb_b / pixel_count),
                     &lab_l, &lab_a, &lab_b);
        out_s2->mean_lab_l = lab_l;
        out_s2->mean_lab_a = lab_a;
        out_s2->mean_lab_b = lab_b;
    }

    if (size > 0) {
        int start_x = roi.left + (width - size) / 2;
        int start_y = roi.top + (height - size) / 2;
        size_t samples = (size_t)size * (size_t)size;
        gray = (double *)malloc(samples * sizeof(*gray));
        if (!gray)
            return FALSE;
        for (y = 0; y < size; y++) {
            if (is_cancel_requested(cancel_requested)) {
                free(gray);
                return FALSE;
            }
            for (x = 0; x < size; x++)
                gray[(size_t)y * (size_t)size + (size_t)x] =
                    roi_y(img, start_x + x, start_y + y, roi);
        }
        if (!ws->fft_buf) {
            ws->fft_buf = (complex_t *)malloc(
                (size_t)FFT_MAX_SIZE * (size_t)FFT_MAX_SIZE *
                sizeof(*ws->fft_buf));
            if (!ws->fft_buf) {
                free(gray);
                return FALSE;
            }
        }
        if (!FFT_Compute2D_Radix2(gray, size, TRUE, ws->fft_buf)) {
            free(gray);
            return FALSE;
        }
        free(gray);
        FFT_CalculateEnergyBands(ws->fft_buf, size,
                                 &out_s2->fft_low_energy,
                                 &out_s2->fft_mid_energy,
                                 &out_s2->fft_high_energy,
                                 &out_s2->fft_high_ratio);
    }
    {
        size_t edge_bytes = (size_t)EDGE_PREVIEW_SIZE *
                            (size_t)EDGE_PREVIEW_SIZE * 4;
        BYTE *edge = (BYTE *)malloc(edge_bytes);
        BYTE *png = NULL;
        size_t png_size = 0;
        if (!edge)
            return FALSE;
        for (y = 0; y < EDGE_PREVIEW_SIZE; y++) {
            int sy = roi.top + (int)((int64_t)y * height /
                                     EDGE_PREVIEW_SIZE);
            BYTE *row = edge + (size_t)y * EDGE_PREVIEW_SIZE * 4;
            if (is_cancel_requested(cancel_requested)) {
                free(edge);
                return FALSE;
            }
            for (x = 0; x < EDGE_PREVIEW_SIZE; x++) {
                int sx = roi.left + (int)((int64_t)x * width /
                                          EDGE_PREVIEW_SIZE);
                double lap = roi_y(img, sx, sy - 1, roi) +
                             roi_y(img, sx - 1, sy, roi) +
                             roi_y(img, sx + 1, sy, roi) +
                             roi_y(img, sx, sy + 1, roi) -
                             4.0 * roi_y(img, sx, sy, roi);
                BYTE intensity = (BYTE)fmin(255.0, fabs(lap) * 0.25);
                row[(size_t)x * 4] = intensity;
                row[(size_t)x * 4 + 1] = intensity;
                row[(size_t)x * 4 + 2] = intensity;
                row[(size_t)x * 4 + 3] = 255;
            }
        }
        if (!Image_EncodePNGMemory(edge, EDGE_PREVIEW_SIZE,
                                   EDGE_PREVIEW_SIZE,
                                   EDGE_PREVIEW_SIZE * 4, &png, &png_size)) {
            free(edge);
            return FALSE;
        }
        free(edge);
        out_s2->edge_png_base64 =
            Report_Base64Encode(png, png_size, &out_s2->edge_png_base64_len);
        Image_FreePNGMemory(png);
        if (!out_s2->edge_png_base64)
            return FALSE;
    }
    return TRUE;
}

BOOL Metrics_ComputeFrequencyAndColor(const image_t *img, RECT rc,
                                      metrics_workspace_t *ws,
                                      metrics_stage2_t *out_s2)
{
    return compute_frequency_and_color(img, rc, ws, out_s2, NULL);
}

static BOOL analyze_roi(const image_t *img, RECT rc, BOOL enable_stage2,
                        volatile BOOL *cancel_requested,
                        metrics_item_result_t *out)
{
    RECT roi;
    metrics_workspace_t workspace;
    BOOL success = FALSE;
    if (!out)
        return FALSE;
    memset(out, 0, sizeof(*out));
    if (!valid_rect(img, rc, &roi))
        return FALSE;
    if (!Metrics_InitWorkspace(&workspace, roi.right - roi.left))
        return FALSE;
    out->image_w = img->w;
    out->image_h = img->h;
    out->source_rect = roi;
    lstrcpynA(out->image_name, img->path, (int)sizeof(out->image_name));
    if (!compute_spatial(img, roi, &workspace, &out->s1, cancel_requested))
        goto done;
    if (enable_stage2) {
        if (is_cancel_requested(cancel_requested))
            goto done;
        if (!compute_frequency_and_color(img, roi, &workspace, &out->s2,
                                         cancel_requested))
            goto done;
        out->has_stage2 = TRUE;
    }
    success = TRUE;
done:
    Metrics_FreeWorkspace(&workspace);
    if (!success)
        Metrics_FreeItemResult(out);
    return success;
}

BOOL Metrics_AnalyzeROI(const image_t *img, RECT rc, BOOL enable_stage2,
                        metrics_item_result_t *out)
{
    return analyze_roi(img, rc, enable_stage2, NULL, out);
}

BOOL Metrics_AnalyzeROI_Cancelable(const image_t *img, RECT rc,
                                   BOOL enable_stage2,
                                   volatile BOOL *cancel_requested,
                                   metrics_item_result_t *out)
{
    return analyze_roi(img, rc, enable_stage2, cancel_requested, out);
}

void Metrics_FreeItemResult(metrics_item_result_t *item)
{
    if (!item)
        return;
    free(item->s2.edge_png_base64);
    item->s2.edge_png_base64 = NULL;
    item->s2.edge_png_base64_len = 0;
}
