#include "metrics_v2.h"

#include <math.h>
#include <string.h>

#include "analyze.h"

#define GAIN_BINS 10001
#define WIDTH_BINS 257
#define CHROMA_BINS 2001
#define CORR_LAGS 16

static void reason(metrics_item_result_t *item, int id, const char *text)
{
    lstrcpynA(item->v2_reason[id], text, METRICS_REASON_LENGTH);
    item->v2[id] = NAN;
}

static double pixel_y(const BYTE *p)
{
    return 0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0];
}

static double clamp_double(double value, double low, double high)
{
    return value < low ? low : (value > high ? high : value);
}

static void lab_at(const image_t *img, const metrics_masks_t *masks,
                   double x, double y, double *l, double *a, double *b)
{
    int ix = (int)floor(x + 0.5);
    int iy = (int)floor(y + 0.5);
    const BYTE *p;
    float fl, fa, fb;
    if (ix < masks->rect.left) ix = masks->rect.left;
    if (ix >= masks->rect.right) ix = masks->rect.right - 1;
    if (iy < masks->rect.top) iy = masks->rect.top;
    if (iy >= masks->rect.bottom) iy = masks->rect.bottom - 1;
    p = img->px + (size_t)iy * (size_t)img->pitch + (size_t)ix * 4;
    rgb_to_lab_f(p[2], p[1], p[0], &fl, &fa, &fb);
    *l = fl; *a = fa; *b = fb;
}

static double histogram_percentile(const uint64_t *hist, size_t bins,
                                   uint64_t total, unsigned percentile)
{
    uint64_t target, sum = 0;
    size_t i;
    if (!total)
        return NAN;
    target = (total - 1) * percentile / 100;
    for (i = 0; i < bins; i++) {
        sum += hist[i];
        if (sum > target)
            return (double)i;
    }
    return (double)(bins - 1);
}

static double metric_gradient(const image_t *img, RECT rect, int x, int y,
                              double *gx_out, double *gy_out)
{
    const int xs[3] = { x - 1, x, x + 1 };
    const int ys[3] = { y - 1, y, y + 1 };
    double p[3][3], gx, gy;
    int ix, iy;
    for (iy = 0; iy < 3; iy++) {
        int py = ys[iy] < rect.top ? rect.top :
                 (ys[iy] >= rect.bottom ? rect.bottom - 1 : ys[iy]);
        const BYTE *row = img->px + (size_t)py * (size_t)img->pitch;
        for (ix = 0; ix < 3; ix++) {
            int px = xs[ix] < rect.left ? rect.left :
                     (xs[ix] >= rect.right ? rect.right - 1 : xs[ix]);
            p[iy][ix] = pixel_y(row + (size_t)px * 4);
        }
    }
    gx = -p[0][0] + p[0][2] - 2.0 * p[1][0] + 2.0 * p[1][2] -
         p[2][0] + p[2][2];
    gy = -p[0][0] - 2.0 * p[0][1] - p[0][2] + p[2][0] +
         2.0 * p[2][1] + p[2][2];
    *gx_out = gx;
    *gy_out = gy;
    return hypot(gx, gy);
}

static double local_mean_y(const image_t *img, RECT rect, int x, int y)
{
    int dx, dy;
    double sum = 0.0;
    for (dy = -1; dy <= 1; dy++) {
        int sy = y + dy;
        if (sy < rect.top) sy = rect.top;
        if (sy >= rect.bottom) sy = rect.bottom - 1;
        for (dx = -1; dx <= 1; dx++) {
            int sx = x + dx;
            const BYTE *p;
            if (sx < rect.left) sx = rect.left;
            if (sx >= rect.right) sx = rect.right - 1;
            p = img->px + (size_t)sy * (size_t)img->pitch +
                (size_t)sx * 4;
            sum += pixel_y(p) / 9.0;
        }
    }
    return sum;
}

static double chroma_at(const image_t *img, int x, int y)
{
    const BYTE *p = img->px + (size_t)y * (size_t)img->pitch +
                    (size_t)x * 4;
    float l, a, b;
    rgb_to_lab_f(p[2], p[1], p[0], &l, &a, &b);
    return hypot(a, b);
}

void MetricsV2_Init(metrics_item_result_t *item)
{
    int i;
    if (!item)
        return;
    for (i = 0; i < METRICS_V2_COUNT; i++)
        reason(item, i, "not computed");
    for (i = 0; i < 3; i++)
        item->v2_edge_count[i] = 0.0;
    item->s2.mean_chroma = NAN;
    item->s2.chroma_p95 = NAN;
    item->s2.chroma_delta_percent = NAN;
    item->s2.hue_delta = NAN;
    item->s2.noise_residual_fwhm = NAN;
    item->s2.chroma_blotch_fwhm = NAN;
    for (i = 0; i < 5; i++) {
        item->s2.gamma_l[i] = NAN;
        lstrcpynA(item->s2.gamma_reason[i], "not computed",
                  METRICS_REASON_LENGTH);
    }
}

BOOL MetricsV2_Compute(const image_t *img, const metrics_masks_t *masks,
                       volatile BOOL *cancel_requested,
                       metrics_item_result_t *item)
{
    uint64_t gain_hist[GAIN_BINS] = { 0 };
    uint64_t tier_gain_hist[3][GAIN_BINS] = { { 0 } };
    uint64_t width_hist[WIDTH_BINS] = { 0 };
    uint64_t chroma_hist[CHROMA_BINS] = { 0 };
    double flat_y = 0.0, flat_y2 = 0.0, flat_c = 0.0, flat_c2 = 0.0;
    double neutral_c = 0.0, neutral_c2 = 0.0, shadow_y = 0.0, shadow_y2 = 0.0;
    double all_c = 0.0, overshoot = 0.0, undershoot = 0.0;
    double gamma_sum[5] = { 0.0 };
    uint64_t gamma_n[5] = { 0 };
    double corr[CORR_LAGS + 1] = { 0.0 };
    uint64_t corr_n[CORR_LAGS + 1] = { 0 };
    uint64_t flat_n = 0, neutral_flat_n = 0, shadow_n = 0;
    uint64_t edge_n = 0, profile_n = 0, width_n = 0, chroma_n = 0;
    uint64_t survival_n[3] = { 0 }, gain_n[3] = { 0 };
    size_t required;
    int x, y, w, h, noise_stride;
    if (!img || !masks || (!masks->classes && !masks->reference) || !item)
        return FALSE;
    w = masks->width;
    h = masks->height;
    required = masks->count / 100;
    if (required < 500) required = 500;
    noise_stride = masks->count < 8000 ? 1 : 4;

    for (y = 0; y < h; y++) {
        if (cancel_requested &&
            InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                       0, 0))
            return FALSE;
        const BYTE *row = img->px + (size_t)(masks->rect.top + y) *
                          (size_t)img->pitch + (size_t)masks->rect.left * 4;
        for (x = 0; x < w; x++) {
            BYTE klass = MetricsMasks_ClassAt(masks, x, y);
            const BYTE *p = row + (size_t)x * 4;
            double Y = pixel_y(p);
            float l, a, b;
            double c, gx = 0.0, gy = 0.0;
            BOOL flat = (klass & METRICS_MASK_FLAT) != 0;
            BOOL neutral = (klass & METRICS_MASK_NEUTRAL) != 0;
            int tier = (klass & METRICS_MASK_TIER_MASK) >>
                       METRICS_MASK_TIER_SHIFT;
            rgb_to_lab_f(p[2], p[1], p[0], &l, &a, &b);
            c = hypot(a, b);
            if (c >= CHROMA_BINS / 10.0)
                chroma_hist[CHROMA_BINS - 1]++;
            else
                chroma_hist[(int)(c * 10.0)]++;
            all_c += c;
            chroma_n++;
            if (flat) {
                flat_y += Y; flat_y2 += Y * Y;
                flat_c += c; flat_c2 += c * c;
                flat_n++;
            }
            if (flat && neutral) {
                neutral_c += c; neutral_c2 += c * c;
                neutral_flat_n++;
            }
            if (Y < 64.0) {
                shadow_y += Y; shadow_y2 += Y * Y; shadow_n++;
            }
            {
                int band = Y < 51.0 ? 0 : (Y < 102.0 ? 1 :
                           (Y < 153.0 ? 2 : (Y < 204.0 ? 3 : 4)));
                gamma_sum[band] += l;
                gamma_n[band]++;
            }
            if (klass & METRICS_MASK_EDGE) {
                double norm, plus[4], minus[4], min_side, max_side;
                double dl, da, db, gain, local_width = 0.0;
                double gradient;
                int step;
                if (tier > 2) tier = 2;
                edge_n++;
                gradient = metric_gradient(img, masks->rect,
                    masks->rect.left + x, masks->rect.top + y, &gx, &gy);
                if (gradient > masks->edge_threshold)
                    survival_n[tier]++;
                norm = hypot(gx, gy);
                if (norm < 1e-8) continue;
                for (step = 0; step < 4; step++) {
                    double px = masks->rect.left + x +
                                gx * (step + 1) / norm;
                    double py = masks->rect.top + y +
                                gy * (step + 1) / norm;
                    lab_at(img, masks, px, py, &plus[step], &da, &db);
                    lab_at(img, masks, 2.0 * (masks->rect.left + x) - px,
                           2.0 * (masks->rect.top + y) - py,
                           &minus[step], &da, &db);
                }
                min_side = (minus[0] + minus[1]) * 0.5;
                max_side = (plus[0] + plus[1]) * 0.5;
                dl = fabs(max_side - min_side);
                if (dl < 0.05) continue;
                gain = gradient / dl;
                {
                    int bin = (int)(clamp_double(gain, 0.0, 1000.0) * 10.0);
                    gain_hist[bin]++;
                    tier_gain_hist[tier][bin]++;
                }
                gain_n[tier]++;
                profile_n++;
                for (step = 0; step < 4; step++) {
                    double value = plus[step];
                    if (value > fmax(min_side, max_side))
                        overshoot += (value - fmax(min_side, max_side)) /
                                     dl * 100.0;
                    value = minus[step];
                    if (value < fmin(min_side, max_side))
                        undershoot += (fmin(min_side, max_side) - value) /
                                      dl * 100.0;
                }
                {
                    double lo = fmin(min_side, max_side);
                    double hi = fmax(min_side, max_side);
                    double c10 = lo + 0.1 * (hi - lo);
                    double c90 = lo + 0.9 * (hi - lo);
                    int first = -1, last = -1;
                    for (step = 0; step < 4; step++) {
                        double v = plus[step];
                        if (v >= c10 && v <= c90) {
                            if (first < 0) first = step;
                            last = step;
                        }
                    }
                    if (first >= 0)
                        local_width = (double)(last - first + 1);
                }
                if (local_width > 0.0) {
                    width_hist[(int)clamp_double(local_width, 0, 256)]++;
                    width_n++;
                }
            }
            if (flat && x % noise_stride == 0 && y % noise_stride == 0) {
                double mean = local_mean_y(img, masks->rect,
                    masks->rect.left + x, masks->rect.top + y);
                double residual = Y - mean;
                for (int lag = 0; lag <= CORR_LAGS && x + lag < w; lag++) {
                    if (MetricsMasks_ClassAt(masks, x + lag, y) &
                        METRICS_MASK_FLAT) {
                        int other_x = masks->rect.left + x + lag;
                        int other_y = masks->rect.top + y;
                        const BYTE *q = img->px +
                            (size_t)other_y * (size_t)img->pitch +
                            (size_t)other_x * 4;
                        double other_residual = pixel_y(q) -
                            local_mean_y(img, masks->rect, other_x, other_y);
                        corr[lag] += residual * other_residual;
                        corr_n[lag]++;
                    }
                }
            }
        }
    }

    if (flat_n >= required) {
        double var = fmax(0.0, flat_y2 / flat_n -
                                (flat_y / flat_n) * (flat_y / flat_n));
        double sigma = sqrt(var);
        item->v2[METRIC_N1_NOISE_SIGMA] = sigma;
        item->v2[METRIC_N1_SNR_DB] = 20.0 * log10((flat_y / flat_n + 1e-9) /
                                                  (sigma + 1e-9));
        item->s1.noise_estimate = sigma;
        item->s1.snr_db = item->v2[METRIC_N1_SNR_DB];
        item->v2[METRIC_C1_CHROMA_SIGMA] =
            sqrt(fmax(0.0, flat_c2 / flat_n -
                      (flat_c / flat_n) * (flat_c / flat_n)));
        item->v2_reason[METRIC_N1_NOISE_SIGMA][0] = '\0';
        item->v2_reason[METRIC_N1_SNR_DB][0] = '\0';
        item->v2_reason[METRIC_C1_CHROMA_SIGMA][0] = '\0';
    } else {
        item->s1.noise_estimate = NAN;
        item->s1.snr_db = NAN;
        reason(item, METRIC_N1_NOISE_SIGMA, "Flat mask below 500px/1%");
        reason(item, METRIC_N1_SNR_DB, "Flat mask below 500px/1%");
        reason(item, METRIC_C1_CHROMA_SIGMA, "Flat mask below 500px/1%");
    }
    if (neutral_flat_n >= required) {
        double chroma_corr[9] = { 0.0 };
        uint64_t chroma_corr_n[9] = { 0 };
        double average = neutral_c / (double)neutral_flat_n;
        int stride = neutral_flat_n < 32000 ? 1 : 8;
        uint64_t corr_required = required / (uint64_t)(stride * stride);
        int lag;
        if (corr_required < 50) corr_required = 50;
        for (y = 0; y < h; y += stride) {
            if (cancel_requested &&
                InterlockedCompareExchange((volatile LONG *)cancel_requested,
                                           0, 0))
                return FALSE;
            for (x = 0; x < w; x += stride) {
                int sx = masks->rect.left + x;
                int sy = masks->rect.top + y;
                double center;
                if ((MetricsMasks_ClassAt(masks, x, y) &
                     (METRICS_MASK_FLAT | METRICS_MASK_NEUTRAL)) !=
                    (METRICS_MASK_FLAT | METRICS_MASK_NEUTRAL))
                    continue;
                center = chroma_at(img, sx, sy) - average;
                for (lag = 0; lag <= 8 && x + lag < w; lag++) {
                    if ((MetricsMasks_ClassAt(masks, x + lag, y) &
                         (METRICS_MASK_FLAT | METRICS_MASK_NEUTRAL)) ==
                        (METRICS_MASK_FLAT | METRICS_MASK_NEUTRAL)) {
                        chroma_corr[lag] += center *
                            (chroma_at(img, sx + lag, sy) - average);
                        chroma_corr_n[lag]++;
                    }
                }
            }
        }
        item->v2[METRIC_C2_CHROMA_SIGMA] =
            sqrt(fmax(0.0, neutral_c2 / neutral_flat_n -
                      (neutral_c / neutral_flat_n) *
                      (neutral_c / neutral_flat_n)));
        item->v2_reason[METRIC_C2_CHROMA_SIGMA][0] = '\0';
        if (chroma_corr_n[0] >= corr_required) {
            double c0 = chroma_corr[0] / (double)chroma_corr_n[0];
            double fwhm = 16.0;
            for (lag = 1; lag <= 8; lag++) {
                double value = chroma_corr_n[lag] ?
                    chroma_corr[lag] / (double)chroma_corr_n[lag] : 0.0;
                if (c0 > 0.0 && value / c0 <= 0.5) {
                    fwhm = 2.0 * lag;
                    break;
                }
            }
            item->v2[METRIC_C2_BLOTCH_FWHM] = fwhm;
            item->s2.chroma_blotch_fwhm = fwhm;
            item->v2_reason[METRIC_C2_BLOTCH_FWHM][0] = '\0';
        } else {
            reason(item, METRIC_C2_BLOTCH_FWHM,
                   "NeutralFlat spatial sample below 500");
        }
    } else {
        reason(item, METRIC_C2_CHROMA_SIGMA,
               "NeutralFlat mask below 500px/1%");
        reason(item, METRIC_C2_BLOTCH_FWHM,
               "NeutralFlat mask below 500px/1%");
    }
    if (shadow_n >= required) {
        item->v2[METRIC_N3_SHADOW_SIGMA] =
            sqrt(fmax(0.0, shadow_y2 / shadow_n -
                      (shadow_y / shadow_n) * (shadow_y / shadow_n)));
        item->v2_reason[METRIC_N3_SHADOW_SIGMA][0] = '\0';
    } else {
        reason(item, METRIC_N3_SHADOW_SIGMA, "Shadow mask below 500px/1%");
    }
    for (x = 0; x < 5; x++) {
        item->s2.gamma_l[x] = gamma_n[x] ?
            gamma_sum[x] / (double)gamma_n[x] : NAN;
        if (gamma_n[x])
            item->s2.gamma_reason[x][0] = '\0';
        else
            lstrcpynA(item->s2.gamma_reason[x], "No samples in luma band",
                      METRICS_REASON_LENGTH);
    }
    if (profile_n >= required) {
        item->v2[METRIC_S1_EDGE_GAIN] =
            histogram_percentile(gain_hist, GAIN_BINS, profile_n, 50) / 10.0;
        item->v2[METRIC_S3_OVERSHOOT] = overshoot / (profile_n * 4.0);
        item->v2[METRIC_S3_UNDERSHOOT] = undershoot / (profile_n * 4.0);
        item->v2_reason[METRIC_S1_EDGE_GAIN][0] = '\0';
        item->v2_reason[METRIC_S3_OVERSHOOT][0] = '\0';
        item->v2_reason[METRIC_S3_UNDERSHOOT][0] = '\0';
    } else {
        reason(item, METRIC_S1_EDGE_GAIN, "Edge mask below 500px/1%");
        reason(item, METRIC_S2_EDGE_WIDTH, "Edge mask below 500px/1%");
        reason(item, METRIC_S3_OVERSHOOT, "Edge mask below 500px/1%");
        reason(item, METRIC_S3_UNDERSHOOT, "Edge mask below 500px/1%");
    }
    if (width_n >= required) {
        item->v2[METRIC_S2_EDGE_WIDTH] =
            histogram_percentile(width_hist, WIDTH_BINS, width_n, 50);
        item->v2_reason[METRIC_S2_EDGE_WIDTH][0] = '\0';
    } else {
        reason(item, METRIC_S2_EDGE_WIDTH,
               "10-90% transition profiles below 500px/1%");
    }
    if (edge_n >= required) {
        item->v2[METRIC_S4_LOW_GAIN] = gain_n[0] >= required ?
            histogram_percentile(tier_gain_hist[0], GAIN_BINS,
                                 gain_n[0], 50) / 10.0 : NAN;
        item->v2[METRIC_S4_MID_GAIN] = gain_n[1] >= required ?
            histogram_percentile(tier_gain_hist[1], GAIN_BINS,
                                 gain_n[1], 50) / 10.0 : NAN;
        item->v2[METRIC_S4_HIGH_GAIN] = gain_n[2] >= required ?
            histogram_percentile(tier_gain_hist[2], GAIN_BINS,
                                 gain_n[2], 50) / 10.0 : NAN;
        item->v2[METRIC_S4_TIER_RATIO] =
            gain_n[0] >= required && gain_n[2] >= required ?
            item->v2[METRIC_S4_LOW_GAIN] /
            fmax(item->v2[METRIC_S4_HIGH_GAIN], 1e-9) : NAN;
        if (gain_n[0] >= required)
            item->v2_reason[METRIC_S4_LOW_GAIN][0] = '\0';
        else
            lstrcpynA(item->v2_reason[METRIC_S4_LOW_GAIN],
                      "No edge samples in tier", METRICS_REASON_LENGTH);
        if (gain_n[1] >= required)
            item->v2_reason[METRIC_S4_MID_GAIN][0] = '\0';
        else
            lstrcpynA(item->v2_reason[METRIC_S4_MID_GAIN],
                      "No edge samples in tier", METRICS_REASON_LENGTH);
        if (gain_n[2] >= required)
            item->v2_reason[METRIC_S4_HIGH_GAIN][0] = '\0';
        else
            lstrcpynA(item->v2_reason[METRIC_S4_HIGH_GAIN],
                      "No edge samples in tier", METRICS_REASON_LENGTH);
        if (gain_n[0] >= required && gain_n[2] >= required)
            item->v2_reason[METRIC_S4_TIER_RATIO][0] = '\0';
        else
            lstrcpynA(item->v2_reason[METRIC_S4_TIER_RATIO],
                      "Low or high edge tier unavailable",
                      METRICS_REASON_LENGTH);
    } else {
        reason(item, METRIC_S4_LOW_GAIN, "Edge mask below 500px/1%");
        reason(item, METRIC_S4_MID_GAIN, "Edge mask below 500px/1%");
        reason(item, METRIC_S4_HIGH_GAIN, "Edge mask below 500px/1%");
        reason(item, METRIC_S4_TIER_RATIO, "Edge mask below 500px/1%");
    }
    for (x = 0; x < 3; x++)
        item->v2_edge_count[x] = (double)survival_n[x];
    if (chroma_n >= required) {
        double p95 = histogram_percentile(chroma_hist, CHROMA_BINS,
                                          chroma_n, 95) / 10.0;
        item->s2.mean_chroma = all_c / (double)chroma_n;
        item->s2.chroma_p95 = p95;
        item->s2.chroma_delta_percent = NAN;
        item->v2[METRIC_K1_CHROMA_MEAN] = item->s2.mean_chroma;
        item->v2[METRIC_K1_CHROMA_P95] = p95;
        reason(item, METRIC_K1_CHROMA_DELTA_PERCENT, "No reference image");
        item->v2_reason[METRIC_K1_CHROMA_MEAN][0] = '\0';
        item->v2_reason[METRIC_K1_CHROMA_P95][0] = '\0';
    } else {
        reason(item, METRIC_K1_CHROMA_MEAN, "ROI below 500px/1%");
        reason(item, METRIC_K1_CHROMA_P95, "ROI below 500px/1%");
        reason(item, METRIC_K1_CHROMA_DELTA_PERCENT, "ROI below 500px/1%");
    }
    {
        uint64_t corr_required = required /
            (uint64_t)(noise_stride * noise_stride);
        if (corr_required < 50) corr_required = 50;
        if (corr_n[0] >= corr_required) {
        double c0 = corr_n[0] ? corr[0] / corr_n[0] : 0.0;
        item->v2[METRIC_N2_RESIDUAL_FWHM] = 0.0;
        for (x = 1; x <= CORR_LAGS; x++) {
            double value = corr_n[x] ? corr[x] / corr_n[x] : 0.0;
            if (c0 > 0.0 && value / c0 <= 0.5) {
                item->v2[METRIC_N2_RESIDUAL_FWHM] = 2.0 * x;
                break;
            }
            item->v2[METRIC_N2_RESIDUAL_FWHM] = 2.0 * x;
        }
        item->s2.noise_residual_fwhm =
            item->v2[METRIC_N2_RESIDUAL_FWHM];
        item->v2_reason[METRIC_N2_RESIDUAL_FWHM][0] = '\0';
        } else {
            reason(item, METRIC_N2_RESIDUAL_FWHM,
                   "Flat residual sample below 500px/1%");
        }
    }
    if (item->has_stage2) {
        item->v2[METRIC_C3_GRAY_CAST] = item->s2.gray_cast_delta_e;
        item->v2_reason[METRIC_C3_GRAY_CAST][0] = '\0';
    } else {
        reason(item, METRIC_C3_GRAY_CAST, "Stage 2 is disabled");
    }
    reason(item, METRIC_K2_HUE_DELTA, "No reference image");
    reason(item, METRIC_S5_LOW_SURVIVAL, "Reference edge baseline unavailable");
    reason(item, METRIC_S5_MID_SURVIVAL, "Reference edge baseline unavailable");
    reason(item, METRIC_S5_HIGH_SURVIVAL, "Reference edge baseline unavailable");
    return TRUE;
}
