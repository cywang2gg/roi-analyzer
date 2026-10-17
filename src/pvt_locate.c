#include "pvt_locate.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PVT_MAX_CROP 512
#define PVT_PI 3.14159265358979323846

typedef struct
{
    double a_v;
    double b_v;
    double a_h;
    double b_h;
} pvt_seam_lines_t;

static int pvt_fit_seams(const double *hx, const double *hy,
                         const double *hw, int h_count,
                         const double *vx, const double *vy,
                         const double *vw, int v_count,
                         double *out_cx, double *out_cy,
                         pvt_seam_lines_t *out_lines);

static void pvt_fail(pvt_target_t *out, const char *reason)
{
    memset(out, 0, sizeof(*out));
    out->reason = reason;
}

static int pvt_luminance(const uint8_t *pixel)
{
    return (77 * pixel[2] + 150 * pixel[1] + 29 * pixel[0] + 128) >> 8;
}

static int pvt_otsu(const unsigned long histogram[256], int count)
{
    int threshold;
    int value;
    unsigned long background_count;
    unsigned long total_sum;
    unsigned long background_sum;
    double best_variance;

    total_sum = 0;
    for (value = 0; value < 256; value++)
    {
        total_sum += (unsigned long)value * histogram[value];
    }
    background_count = 0;
    background_sum = 0;
    best_variance = -1.0;
    threshold = 0;
    for (value = 0; value < 256; value++)
    {
        unsigned long foreground_count;
        double background_mean;
        double foreground_mean;
        double difference;
        double variance;

        background_count += histogram[value];
        background_sum += (unsigned long)value * histogram[value];
        foreground_count = (unsigned long)count - background_count;
        if (background_count == 0 || foreground_count == 0)
        {
            continue;
        }
        background_mean = (double)background_sum / (double)background_count;
        foreground_mean = (double)(total_sum - background_sum) /
                          (double)foreground_count;
        difference = background_mean - foreground_mean;
        variance = (double)background_count * (double)foreground_count *
                   difference * difference;
        if (variance > best_variance)
        {
            best_variance = variance;
            threshold = value;
        }
    }
    return threshold;
}

static int pvt_set_means(const uint8_t *bgra, int width, int height, int stride,
                         float cx, float cy, float radius, double means[4])
{
    int phase_index;
    double best_score;
    double best_means[4];
    int sector;

    best_score = -1.0;
    for (phase_index = 0; phase_index < 2; phase_index++)
    {
        double sums[4];
        int counts[4];
        int radius_index;
        int angle_index;
        double phase;
        double score;
        double opposite_a;
        double opposite_b;
        double adjacent;

        for (sector = 0; sector < 4; sector++)
        {
            sums[sector] = 0.0;
            counts[sector] = 0;
        }
        phase = phase_index == 0 ? 0.0 : PVT_PI / 2.0;
        for (radius_index = 0; radius_index < 3; radius_index++)
        {
            double sample_radius;

            sample_radius = radius * (0.35 + 0.25 * (double)radius_index);
            for (angle_index = 0; angle_index < 24; angle_index++)
            {
                double angle;
                double x;
                double y;
                int image_x;
                int image_y;
                size_t offset;
                int luminance;
                int sector_index;
                double sector_angle;
                double sector_distance;

                angle = (2.0 * PVT_PI * (double)angle_index / 24.0) + phase;
                x = (double)cx + cos(angle) * sample_radius;
                y = (double)cy + sin(angle) * sample_radius;
                image_x = (int)floor(x + 0.5);
                image_y = (int)floor(y + 0.5);
                if (image_x < 0 || image_x >= width ||
                    image_y < 0 || image_y >= height)
                {
                    return 0;
                }
                angle = fmod(angle + 2.0 * PVT_PI, 2.0 * PVT_PI);
                offset = (size_t)image_y * (size_t)stride +
                         (size_t)image_x * 4U;
                luminance = pvt_luminance(bgra + offset);
                sector_angle = fmod(angle - PVT_PI / 4.0 +
                                     2.0 * PVT_PI, 2.0 * PVT_PI);
                sector_index = (int)floor(
                    sector_angle / (PVT_PI / 2.0) + 0.5);
                if (sector_index > 3)
                {
                    sector_index = 0;
                }
                sector_distance = fabs(
                    sector_angle - (double)sector_index * (PVT_PI / 2.0));
                if (sector_distance > PVT_PI)
                {
                    sector_distance = 2.0 * PVT_PI - sector_distance;
                }
                if (sector_distance > PVT_PI / 8.0)
                {
                    continue;
                }
                sums[sector_index] += (double)luminance;
                counts[sector_index]++;
            }
        }
        for (sector = 0; sector < 4; sector++)
        {
            if (counts[sector] == 0)
            {
                return 0;
            }
            means[sector] = sums[sector] / (double)counts[sector];
        }
        opposite_a = fabs(means[0] - means[2]);
        opposite_b = fabs(means[1] - means[3]);
        adjacent = (fabs(means[0] - means[1]) +
                    fabs(means[1] - means[2]) +
                    fabs(means[2] - means[3]) +
                    fabs(means[3] - means[0])) / 4.0;
        score = adjacent - fmax(opposite_a, opposite_b);
        if (score > best_score)
        {
            best_score = score;
            for (sector = 0; sector < 4; sector++)
            {
                best_means[sector] = means[sector];
            }
        }
    }
    for (sector = 0; sector < 4; sector++)
    {
        means[sector] = best_means[sector];
    }
    return best_score >= 50.0;
}

#define PVT_LOCATE_CUTS_HALF 5
#define PVT_LOCATE_MAX_ITER 3
#define PVT_LOCATE_CONV_EPS 0.05

static int pvt_compare_double(const void *left, const void *right)
{
    double a;
    double b;

    a = *(const double *)left;
    b = *(const double *)right;
    if (a < b)
    {
        return -1;
    }
    if (a > b)
    {
        return 1;
    }
    return 0;
}

static double pvt_median_double(const double *values, int count)
{
    double *sorted;
    int middle;
    double result;

    if (values == NULL || count <= 0)
    {
        return 0.0;
    }
    sorted = (double *)malloc((size_t)count * sizeof(*sorted));
    if (sorted == NULL)
    {
        return 0.0;
    }
    memcpy(sorted, values, (size_t)count * sizeof(*sorted));
    qsort(sorted, (size_t)count, sizeof(*sorted), pvt_compare_double);
    middle = count / 2;
    if ((count & 1) != 0)
    {
        result = sorted[middle];
    }
    else
    {
        result = 0.5 * (sorted[middle - 1] + sorted[middle]);
    }
    free(sorted);
    return result;
}

static int pvt_fit_wls_line(const double *indep, const double *dep,
                            const double *weights, const uint8_t *inlier_mask,
                            int count, double *out_slope, double *out_intercept)
{
    int i;
    double sum_w;
    double sum_indep;
    double sum_dep;
    double sum_indep2;
    double sum_indep_dep;
    double det;

    if (indep == NULL || dep == NULL || weights == NULL ||
        out_slope == NULL || out_intercept == NULL || count <= 0)
    {
        return 0;
    }
    sum_w = 0.0;
    sum_indep = 0.0;
    sum_dep = 0.0;
    sum_indep2 = 0.0;
    sum_indep_dep = 0.0;
    for (i = 0; i < count; i++)
    {
        double w;
        double x;
        double y;

        if (inlier_mask != NULL && inlier_mask[i] == 0)
        {
            continue;
        }
        w = weights[i];
        x = indep[i];
        y = dep[i];
        sum_w += w;
        sum_indep += w * x;
        sum_dep += w * y;
        sum_indep2 += w * x * x;
        sum_indep_dep += w * x * y;
    }
    if (sum_w <= 0.0)
    {
        return 0;
    }
    det = sum_w * sum_indep2 - sum_indep * sum_indep;
    if (fabs(det) < 1e-6)
    {
        return 0;
    }
    *out_slope = (sum_w * sum_indep_dep - sum_indep * sum_dep) / det;
    *out_intercept = (sum_indep2 * sum_dep - sum_indep * sum_indep_dep) / det;
    return 1;
}

static int pvt_refine_center_knife_edge(const uint8_t *bgra, int width,
                                        int height, int stride,
                                        float coarse_cx, float coarse_cy,
                                        float coarse_r,
                                        float *out_cx, float *out_cy,
                                        pvt_seam_lines_t *out_lines)
{
    double cx;
    double cy;
    double best_cx;
    double best_cy;
    pvt_seam_lines_t best_lines;
    int have_best;
    double alpha_values[PVT_LOCATE_CUTS_HALF];
    int iteration;
    int k;
    int half_span;

    if (bgra == NULL || out_cx == NULL || out_cy == NULL ||
        width <= 0 || height <= 0 || stride < width * 4)
    {
        return 0;
    }
    alpha_values[0] = 0.25;
    alpha_values[1] = 0.375;
    alpha_values[2] = 0.50;
    alpha_values[3] = 0.625;
    alpha_values[4] = 0.75;
    cx = (double)coarse_cx;
    cy = (double)coarse_cy;
    best_cx = cx;
    best_cy = cy;
    have_best = 0;
    half_span = (int)floor(0.55 * (double)coarse_r + 0.5);
    if (half_span < 5)
    {
        half_span = 5;
    }
    if (half_span > 31)
    {
        half_span = 31;
    }
    for (iteration = 0; iteration < PVT_LOCATE_MAX_ITER; iteration++)
    {
        double hx[20];
        double hy[20];
        double hw[20];
        double vx[20];
        double vy[20];
        double vw[20];
        int h_count;
        int v_count;
        double new_cx;
        double new_cy;
        double delta;
        pvt_seam_lines_t iteration_lines;

        h_count = 0;
        v_count = 0;
        for (k = 0; k < PVT_LOCATE_CUTS_HALF; k++)
        {
            int image_y;
            int image_x;
            int x_start;
            int y_start;
            int len;
            int s;
            double alpha;
            double step;
            double edge_pos;
            double x_value;
            double y_value;

            alpha = alpha_values[k];
            len = 2 * half_span + 1;
            image_y = (int)floor(cy - alpha * (double)coarse_r + 0.5);
            if (image_y >= 0 && image_y < height && h_count < 20)
            {
                double *profile;

                profile = (double *)malloc((size_t)len * sizeof(*profile));
                if (profile == NULL)
                {
                    return 0;
                }
                x_start = (int)floor(cx + 0.5) - half_span;
                for (s = 0; s < len; s++)
                {
                    int sample_x;
                    int sample_y;
                    int luminance;

                    sample_x = x_start + s;
                    sample_y = image_y;
                    if (sample_x < 0 || sample_x >= width)
                    {
                        profile[s] = 0.0;
                        continue;
                    }
                    luminance = pvt_luminance(
                        bgra + (size_t)sample_y * (size_t)stride +
                        (size_t)sample_x * 4U);
                    profile[s] = (double)luminance;
                }
                if (Pvt_FindEdgeSubpixel(profile, len, &edge_pos, &step))
                {
                    x_value = (double)x_start + 0.5 + edge_pos;
                    y_value = (double)image_y + 0.5;
                    hx[h_count] = x_value;
                    hy[h_count] = y_value;
                    hw[h_count] = step;
                    h_count++;
                }
                free(profile);
            }
            image_y = (int)floor(cy + alpha * (double)coarse_r + 0.5);
            if (image_y >= 0 && image_y < height && h_count < 20)
            {
                double *profile;

                profile = (double *)malloc((size_t)len * sizeof(*profile));
                if (profile == NULL)
                {
                    return 0;
                }
                x_start = (int)floor(cx + 0.5) - half_span;
                for (s = 0; s < len; s++)
                {
                    int sample_x;
                    int sample_y;
                    int luminance;

                    sample_x = x_start + s;
                    sample_y = image_y;
                    if (sample_x < 0 || sample_x >= width)
                    {
                        profile[s] = 0.0;
                        continue;
                    }
                    luminance = pvt_luminance(
                        bgra + (size_t)sample_y * (size_t)stride +
                        (size_t)sample_x * 4U);
                    profile[s] = (double)luminance;
                }
                if (Pvt_FindEdgeSubpixel(profile, len, &edge_pos, &step))
                {
                    x_value = (double)x_start + 0.5 + edge_pos;
                    y_value = (double)image_y + 0.5;
                    hx[h_count] = x_value;
                    hy[h_count] = y_value;
                    hw[h_count] = step;
                    h_count++;
                }
                free(profile);
            }
            image_x = (int)floor(cx - alpha * (double)coarse_r + 0.5);
            if (image_x >= 0 && image_x < width && v_count < 20)
            {
                double *profile;

                profile = (double *)malloc((size_t)len * sizeof(*profile));
                if (profile == NULL)
                {
                    return 0;
                }
                y_start = (int)floor(cy + 0.5) - half_span;
                for (s = 0; s < len; s++)
                {
                    int sample_x;
                    int sample_y;
                    int luminance;

                    sample_x = image_x;
                    sample_y = y_start + s;
                    if (sample_y < 0 || sample_y >= height)
                    {
                        profile[s] = 0.0;
                        continue;
                    }
                    luminance = pvt_luminance(
                        bgra + (size_t)sample_y * (size_t)stride +
                        (size_t)sample_x * 4U);
                    profile[s] = (double)luminance;
                }
                if (Pvt_FindEdgeSubpixel(profile, len, &edge_pos, &step))
                {
                    x_value = (double)image_x + 0.5;
                    y_value = (double)y_start + 0.5 + edge_pos;
                    vx[v_count] = x_value;
                    vy[v_count] = y_value;
                    vw[v_count] = step;
                    v_count++;
                }
                free(profile);
            }
            image_x = (int)floor(cx + alpha * (double)coarse_r + 0.5);
            if (image_x >= 0 && image_x < width && v_count < 20)
            {
                double *profile;

                profile = (double *)malloc((size_t)len * sizeof(*profile));
                if (profile == NULL)
                {
                    return 0;
                }
                y_start = (int)floor(cy + 0.5) - half_span;
                for (s = 0; s < len; s++)
                {
                    int sample_x;
                    int sample_y;
                    int luminance;

                    sample_x = image_x;
                    sample_y = y_start + s;
                    if (sample_y < 0 || sample_y >= height)
                    {
                        profile[s] = 0.0;
                        continue;
                    }
                    luminance = pvt_luminance(
                        bgra + (size_t)sample_y * (size_t)stride +
                        (size_t)sample_x * 4U);
                    profile[s] = (double)luminance;
                }
                if (Pvt_FindEdgeSubpixel(profile, len, &edge_pos, &step))
                {
                    x_value = (double)image_x + 0.5;
                    y_value = (double)y_start + 0.5 + edge_pos;
                    vx[v_count] = x_value;
                    vy[v_count] = y_value;
                    vw[v_count] = step;
                    v_count++;
                }
                free(profile);
            }
        }
        if (h_count < 4 || v_count < 4)
        {
            return 0;
        }
        if (!pvt_fit_seams(hx, hy, hw, h_count,
                           vx, vy, vw, v_count,
                           &new_cx, &new_cy, &iteration_lines))
        {
            return 0;
        }
        if (hypot(new_cx - coarse_cx, new_cy - coarse_cy) >
            0.40 * (double)coarse_r)
        {
            break;
        }
        best_cx = new_cx;
        best_cy = new_cy;
        best_lines = iteration_lines;
        have_best = 1;
        delta = hypot(new_cx - cx, new_cy - cy);
        cx = new_cx;
        cy = new_cy;
        if (delta <= PVT_LOCATE_CONV_EPS)
        {
            break;
        }
    }
    if (!have_best)
    {
        return 0;
    }
    *out_cx = (float)best_cx;
    *out_cy = (float)best_cy;
    if (out_lines != NULL)
    {
        *out_lines = best_lines;
    }
    return 1;
}

int Pvt_FindEdgeSubpixel(const double *profile, int len,
                         double *edge_pos, double *out_step)
{
    int platform_count;
    int i;
    int best_index;
    double start_mean;
    double end_mean;
    double T_mid;
    double best_delta;
    double best_center_distance;
    double step;

    if (profile == NULL || edge_pos == NULL || out_step == NULL || len < 10)
    {
        return 0;
    }
    platform_count = (len + 4) / 5;
    if (platform_count < 2)
    {
        platform_count = 2;
    }
    start_mean = 0.0;
    for (i = 0; i < platform_count; i++)
    {
        start_mean += profile[i];
    }
    start_mean /= (double)platform_count;
    end_mean = 0.0;
    for (i = len - platform_count; i < len; i++)
    {
        end_mean += profile[i];
    }
    end_mean /= (double)platform_count;
    step = fabs(end_mean - start_mean);
    if (step < 20.0)
    {
        return 0;
    }
    T_mid = 0.5 * (start_mean + end_mean);
    best_index = -1;
    best_delta = -1.0;
    best_center_distance = 0.0;
    for (i = platform_count - 1; i <= len - platform_count - 1; i++)
    {
        int condition;
        double delta;
        double center_distance;

        condition = 0;
        if (end_mean > start_mean)
        {
            condition = profile[i] <= T_mid && profile[i + 1] > T_mid;
        }
        else if (start_mean > end_mean)
        {
            condition = profile[i] >= T_mid && profile[i + 1] < T_mid;
        }
        if (!condition)
        {
            continue;
        }
        delta = fabs(profile[i + 1] - profile[i]);
        center_distance = fabs((double)i + 0.5 - 0.5 * (double)(len - 1));
        if (best_index < 0 || delta > best_delta + 1e-12 ||
            (fabs(delta - best_delta) <= 1e-12 &&
             center_distance < best_center_distance))
        {
            best_index = i;
            best_delta = delta;
            best_center_distance = center_distance;
        }
    }
    if (best_index < 0)
    {
        return 0;
    }
    {
        double frac;
        double edge_index;
        int i_edge;
        double slope_two_px;

        frac = (T_mid - profile[best_index]) /
               (profile[best_index + 1] - profile[best_index]);
        edge_index = (double)best_index + frac;
        if (edge_index < 1.0 || edge_index > (double)len - 2.0)
        {
            return 0;
        }
        i_edge = (int)floor(edge_index + 0.5);
        if (i_edge <= 0 || i_edge >= len - 1)
        {
            return 0;
        }
        slope_two_px = profile[i_edge + 1] - profile[i_edge - 1];
        if ((end_mean > start_mean && slope_two_px < 0.25 * step) ||
            (start_mean > end_mean && -slope_two_px < 0.25 * step))
        {
            return 0;
        }
        *edge_pos = edge_index;
        *out_step = step;
    }
    return 1;
}

static int pvt_fit_seams(const double *hx, const double *hy,
                         const double *hw, int h_count,
                         const double *vx, const double *vy,
                         const double *vw, int v_count,
                         double *out_cx, double *out_cy,
                         pvt_seam_lines_t *out_lines)
{
    int i;
    int keep_count;
    int h_used;
    int v_used;
    double median_h;
    double median_v;
    double median_h_axis;
    double median_v_axis;
    double h_med;
    double v_med;
    double *h_inlier;
    double *v_inlier;
    uint8_t *h_mask;
    uint8_t *v_mask;
    double a_v;
    double b_v;
    double a_h;
    double b_h;
    double denom;
    double cx;
    double cy;

    if (hx == NULL || hy == NULL || hw == NULL || vx == NULL ||
        vy == NULL || vw == NULL || out_cx == NULL || out_cy == NULL ||
        h_count <= 0 || v_count <= 0)
    {
        return 0;
    }
    h_inlier = (double *)malloc((size_t)h_count * sizeof(*h_inlier));
    v_inlier = (double *)malloc((size_t)v_count * sizeof(*v_inlier));
    h_mask = (uint8_t *)malloc((size_t)h_count * sizeof(*h_mask));
    v_mask = (uint8_t *)malloc((size_t)v_count * sizeof(*v_mask));
    if (h_inlier == NULL || v_inlier == NULL || h_mask == NULL || v_mask == NULL)
    {
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        return 0;
    }
    h_med = pvt_median_double(hw, h_count);
    v_med = pvt_median_double(vw, v_count);
    for (i = 0; i < h_count; i++)
    {
        h_mask[i] = (uint8_t)(hw[i] >= fmax(20.0, 0.5 * h_med) ? 1U : 0U);
    }
    for (i = 0; i < v_count; i++)
    {
        v_mask[i] = (uint8_t)(vw[i] >= fmax(20.0, 0.5 * v_med) ? 1U : 0U);
    }
    h_used = 0;
    for (i = 0; i < h_count; i++)
    {
        if (h_mask[i])
        {
            h_inlier[h_used++] = hx[i];
        }
    }
    if (h_used < 4)
    {
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        return 0;
    }
    v_used = 0;
    for (i = 0; i < v_count; i++)
    {
        if (v_mask[i])
        {
            v_inlier[v_used++] = vy[i];
        }
    }
    if (v_used < 4)
    {
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        return 0;
    }
    median_h = pvt_median_double(h_inlier, h_used);
    median_v = pvt_median_double(v_inlier, v_used);
    /* Reuse the work buffers to obtain the axis medians: the collinearity gate
       needs the crossing median AND the centre of the cut set. */
    h_used = 0;
    for (i = 0; i < h_count; i++)
    {
        if (h_mask[i])
        {
            h_inlier[h_used++] = hy[i];
        }
    }
    median_h_axis = pvt_median_double(h_inlier, h_used);
    v_used = 0;
    for (i = 0; i < v_count; i++)
    {
        if (v_mask[i])
        {
            v_inlier[v_used++] = vx[i];
        }
    }
    median_v_axis = pvt_median_double(v_inlier, v_used);
    for (i = 0; i < h_count; i++)
    {
        double tol;
        tol = fmax(2.0, 0.20 * fabs(hy[i] - median_h_axis));
        if (h_mask[i] && fabs(hx[i] - median_h) > tol)
        {
            h_mask[i] = 0;
        }
    }
    for (i = 0; i < v_count; i++)
    {
        double tol;
        tol = fmax(2.0, 0.20 * fabs(vx[i] - median_v_axis));
        if (v_mask[i] && fabs(vy[i] - median_v) > tol)
        {
            v_mask[i] = 0;
        }
    }
    keep_count = 0;
    for (i = 0; i < h_count; i++)
    {
        if (h_mask[i])
        {
            keep_count++;
        }
    }
    if (keep_count < 4)
    {
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        return 0;
    }
    keep_count = 0;
    for (i = 0; i < v_count; i++)
    {
        if (v_mask[i])
        {
            keep_count++;
        }
    }
    if (keep_count < 4)
    {
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        return 0;
    }
    {
        double slope;
        double intercept;
        double residual;
        uint8_t *h_residual_mask;
        uint8_t *v_residual_mask;
        int h_valid;
        int v_valid;

        h_residual_mask = (uint8_t *)malloc((size_t)h_count * sizeof(*h_residual_mask));
        v_residual_mask = (uint8_t *)malloc((size_t)v_count * sizeof(*v_residual_mask));
        if (h_residual_mask == NULL || v_residual_mask == NULL)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        h_valid = 0;
        for (i = 0; i < h_count; i++)
        {
            if (h_mask[i])
            {
                h_residual_mask[i] = 1U;
                h_valid++;
            }
            else
            {
                h_residual_mask[i] = 0U;
            }
        }
        if (h_valid < 4)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        if (!pvt_fit_wls_line(hy, hx, hw, h_mask, h_count, &slope, &intercept))
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        a_v = slope;
        b_v = intercept;
        for (i = 0; i < h_count; i++)
        {
            if (!h_mask[i])
            {
                continue;
            }
            residual = fabs(hx[i] - (a_v * hy[i] + b_v));
            if (residual > 1.5)
            {
                h_mask[i] = 0U;
            }
        }
        if (pvt_fit_wls_line(hy, hx, hw, h_mask, h_count, &a_v, &b_v) == 0)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        if (fabs(a_v) > 0.45)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        v_valid = 0;
        for (i = 0; i < v_count; i++)
        {
            if (v_mask[i])
            {
                v_residual_mask[i] = 1U;
                v_valid++;
            }
            else
            {
                v_residual_mask[i] = 0U;
            }
        }
        if (v_valid < 4)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        if (!pvt_fit_wls_line(vx, vy, vw, v_mask, v_count, &slope, &intercept))
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        a_h = slope;
        b_h = intercept;
        for (i = 0; i < v_count; i++)
        {
            if (!v_mask[i])
            {
                continue;
            }
            residual = fabs(vy[i] - (a_h * vx[i] + b_h));
            if (residual > 1.5)
            {
                v_mask[i] = 0U;
            }
        }
        if (pvt_fit_wls_line(vx, vy, vw, v_mask, v_count, &a_h, &b_h) == 0)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        if (fabs(a_h) > 0.45)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        denom = 1.0 - a_v * a_h;
        if (fabs(denom) < 0.5)
        {
            free(h_inlier);
            free(v_inlier);
            free(h_mask);
            free(v_mask);
            free(h_residual_mask);
            free(v_residual_mask);
            return 0;
        }
        cx = (b_v + a_v * b_h) / denom;
        cy = (b_h + a_h * b_v) / denom;
        free(h_inlier);
        free(v_inlier);
        free(h_mask);
        free(v_mask);
        free(h_residual_mask);
        free(v_residual_mask);
        *out_cx = cx;
        *out_cy = cy;
        if (out_lines != NULL)
        {
            out_lines->a_v = a_v;
            out_lines->b_v = b_v;
            out_lines->a_h = a_h;
            out_lines->b_h = b_h;
        }
        return 1;
    }
    free(h_inlier);
    free(v_inlier);
    free(h_mask);
    free(v_mask);
    return 0;
}

int Pvt_FitLineIntersection(const double *hx, const double *hy,
                            const double *hw, int h_count,
                            const double *vx, const double *vy,
                            const double *vw, int v_count,
                            double *out_cx, double *out_cy)
{
    return pvt_fit_seams(hx, hy, hw, h_count, vx, vy, vw, v_count,
                         out_cx, out_cy, NULL);
}

int Pvt_AnalyzeProfile(const double *profile, int len, double *contrast,
                       double *overshoot_pct, double *undershoot_pct)
{
    int platform_count;
    int i;
    double start_min;
    double start_max;
    double end_min;
    double end_max;
    double start_mean;
    double end_mean;
    double low;
    double high;
    double step;
    double minimum;
    double maximum;

    if (contrast == NULL || overshoot_pct == NULL || undershoot_pct == NULL)
    {
        return 0;
    }
    *contrast = 0.0;
    *overshoot_pct = 0.0;
    *undershoot_pct = 0.0;
    if (profile == NULL || len < 10)
    {
        return 0;
    }
    platform_count = (len + 4) / 5;
    if (platform_count < 2)
    {
        platform_count = 2;
    }
    start_min = profile[0];
    start_max = profile[0];
    end_min = profile[len - platform_count];
    end_max = end_min;
    start_mean = 0.0;
    end_mean = 0.0;
    minimum = profile[0];
    maximum = profile[0];
    for (i = 0; i < len; i++)
    {
        if (!isfinite(profile[i]))
        {
            return 0;
        }
        if (profile[i] < minimum)
        {
            minimum = profile[i];
        }
        if (profile[i] > maximum)
        {
            maximum = profile[i];
        }
        if (i < platform_count)
        {
            if (profile[i] < start_min)
            {
                start_min = profile[i];
            }
            if (profile[i] > start_max)
            {
                start_max = profile[i];
            }
            start_mean += profile[i];
        }
        if (i >= len - platform_count)
        {
            if (profile[i] < end_min)
            {
                end_min = profile[i];
            }
            if (profile[i] > end_max)
            {
                end_max = profile[i];
            }
            end_mean += profile[i];
        }
    }
    start_mean /= (double)platform_count;
    end_mean /= (double)platform_count;
    step = fabs(end_mean - start_mean);
    if (step < 20.0 ||
        start_max - start_min > fmax(8.0, step * 0.10) ||
        end_max - end_min > fmax(8.0, step * 0.10))
    {
        return 0;
    }
    low = fmin(start_mean, end_mean);
    high = fmax(start_mean, end_mean);
    if (high + low <= 0.0)
    {
        return 0;
    }
    *contrast = (high - low) / (high + low);
    *overshoot_pct = fmax(0.0, maximum - high) / step * 100.0;
    *undershoot_pct = fmax(0.0, low - minimum) / step * 100.0;
    return 1;
}

int Pvt_LocateAndMeasure(const uint8_t *bgra, int width, int height, int stride,
                         const pvt_box_t *box, pvt_target_t *out)
{
    int crop_x1;
    int crop_y1;
    int crop_x2;
    int crop_y2;
    int crop_width;
    int crop_height;
    int pixel_count;
    int x;
    int y;
    int threshold;
    int best_found;
    int used_fallback;
    double best_distance;
    double best_area;
    double best_m10;
    double best_m01;
    double best_perimeter;
    unsigned long histogram[256];
    uint8_t *mask;
    uint32_t *queue;
    int row_min[PVT_MAX_CROP];
    int row_max[PVT_MAX_CROP];
    double crop_area;

    if (out == NULL)
    {
        return 0;
    }
    pvt_fail(out, "invalid_input");
    if (bgra == NULL || box == NULL || width <= 0 || height <= 0 ||
        width > INT_MAX / 4 || stride < width * 4 ||
        !isfinite((double)box->x1) || !isfinite((double)box->y1) ||
        !isfinite((double)box->x2) || !isfinite((double)box->y2) ||
        box->x2 <= box->x1 || box->y2 <= box->y1)
    {
        return 0;
    }
    {
        double box_width;
        double box_height;
        double expanded_x1;
        double expanded_y1;
        double expanded_x2;
        double expanded_y2;

        box_width = (double)box->x2 - (double)box->x1;
        box_height = (double)box->y2 - (double)box->y1;
        expanded_x1 = fmax(0.0, (double)box->x1 - 0.30 * box_width);
        expanded_y1 = fmax(0.0, (double)box->y1 - 0.30 * box_height);
        expanded_x2 = fmin((double)width, (double)box->x2 + 0.30 * box_width);
        expanded_y2 = fmin((double)height, (double)box->y2 + 0.30 * box_height);
        crop_x1 = (int)floor(expanded_x1);
        crop_y1 = (int)floor(expanded_y1);
        crop_x2 = (int)ceil(expanded_x2);
        crop_y2 = (int)ceil(expanded_y2);
    }
    crop_width = crop_x2 - crop_x1;
    crop_height = crop_y2 - crop_y1;
    if (crop_width <= 0 || crop_height <= 0)
    {
        return 0;
    }
    if (crop_width > PVT_MAX_CROP || crop_height > PVT_MAX_CROP)
    {
        pvt_fail(out, "box_too_large");
        return 0;
    }
    pixel_count = crop_width * crop_height;
    mask = (uint8_t *)malloc((size_t)pixel_count);
    queue = (uint32_t *)malloc((size_t)pixel_count * sizeof(*queue));
    if (mask == NULL || queue == NULL)
    {
        free(mask);
        free(queue);
        pvt_fail(out, "allocation_failed");
        return 0;
    }
    memset(histogram, 0, sizeof(histogram));
    {
        /*
         * Otsu must see the TARGET, not the chart background. Sampling the
         * 30%-expanded crop produces a three-mode histogram (target black /
         * background / target white) that can make Otsu split between the
         * background and the white quadrants, which would mark the whole
         * background "dark" and destroy the fallback dark area and r_eq.
         * Build the histogram inside the YOLO box, then apply the resulting
         * threshold to the expanded crop below.
         */
        int box_x1;
        int box_y1;
        int box_x2;
        int box_y2;
        int box_pixels = 0;

        box_x1 = (int)floor((double)box->x1) - crop_x1;
        box_y1 = (int)floor((double)box->y1) - crop_y1;
        box_x2 = (int)ceil((double)box->x2) - crop_x1;
        box_y2 = (int)ceil((double)box->y2) - crop_y1;
        if (box_x1 < 0)
        {
            box_x1 = 0;
        }
        if (box_y1 < 0)
        {
            box_y1 = 0;
        }
        if (box_x2 > crop_width - 1)
        {
            box_x2 = crop_width - 1;
        }
        if (box_y2 > crop_height - 1)
        {
            box_y2 = crop_height - 1;
        }
        for (y = box_y1; y <= box_y2; y++)
        {
            for (x = box_x1; x <= box_x2; x++)
            {
                int image_x;
                int image_y;
                int luminance;

                image_x = crop_x1 + x;
                image_y = crop_y1 + y;
                luminance = pvt_luminance(
                    bgra + (size_t)image_y * (size_t)stride +
                    (size_t)image_x * 4U);
                histogram[luminance]++;
                ++box_pixels;
            }
        }
        if (box_pixels >= 16)
        {
            threshold = pvt_otsu(histogram, box_pixels);
        }
        else
        {
            memset(histogram, 0, sizeof(histogram));
            for (y = 0; y < crop_height; y++)
            {
                for (x = 0; x < crop_width; x++)
                {
                    int image_x;
                    int image_y;

                    image_x = crop_x1 + x;
                    image_y = crop_y1 + y;
                    histogram[pvt_luminance(
                        bgra + (size_t)image_y * (size_t)stride +
                        (size_t)image_x * 4U)]++;
                }
            }
            threshold = pvt_otsu(histogram, pixel_count);
        }
    }
    for (y = 0; y < crop_height; y++)
    {
        for (x = 0; x < crop_width; x++)
        {
            int image_x;
            int image_y;
            int luminance;

            image_x = crop_x1 + x;
            image_y = crop_y1 + y;
            luminance = pvt_luminance(
                bgra + (size_t)image_y * (size_t)stride +
                (size_t)image_x * 4U);
            mask[y * crop_width + x] = (uint8_t)(luminance <= threshold ? 1 : 0);
        }
    }

    best_found = 0;
    used_fallback = 0;
    best_distance = 0.0;
    best_area = 0.0;
    best_m10 = 0.0;
    best_m01 = 0.0;
    best_perimeter = 0.0;
    crop_area = (double)pixel_count;
    for (y = 0; y < crop_height; y++)
    {
        int start_x;

        for (start_x = 0; start_x < crop_width; start_x++)
        {
            int head;
            int tail;
            int component_min_x;
            int component_min_y;
            int component_max_x;
            int component_max_y;
            int dy;
            int dx;
            double filled_area;
            double filled_m10;
            double filled_m01;
            double silhouette_perimeter;
            double bbox_area;
            double circularity;
            double fill_ratio;
            double distance;
            uint32_t start_index;

            start_index = (uint32_t)(y * crop_width + start_x);
            if (mask[start_index] != 1)
            {
                continue;
            }
            for (dy = 0; dy < crop_height; dy++)
            {
                row_min[dy] = crop_width;
                row_max[dy] = -1;
            }
            head = 0;
            tail = 0;
            queue[tail++] = start_index;
            mask[start_index] = 2;
            component_min_x = start_x;
            component_min_y = y;
            component_max_x = start_x;
            component_max_y = y;
            while (head < tail)
            {
                uint32_t index;
                int current_x;
                int current_y;

                index = queue[head++];
                current_x = (int)(index % (uint32_t)crop_width);
                current_y = (int)(index / (uint32_t)crop_width);
                if (current_x < component_min_x)
                {
                    component_min_x = current_x;
                }
                if (current_x > component_max_x)
                {
                    component_max_x = current_x;
                }
                if (current_y < component_min_y)
                {
                    component_min_y = current_y;
                }
                if (current_y > component_max_y)
                {
                    component_max_y = current_y;
                }
                if (current_x < row_min[current_y])
                {
                    row_min[current_y] = current_x;
                }
                if (current_x > row_max[current_y])
                {
                    row_max[current_y] = current_x;
                }
                for (dy = -1; dy <= 1; dy++)
                {
                    int next_y;

                    next_y = current_y + dy;
                    if (next_y < 0 || next_y >= crop_height)
                    {
                        continue;
                    }
                    for (dx = -1; dx <= 1; dx++)
                    {
                        int next_x;
                        uint32_t next_index;

                        next_x = current_x + dx;
                        if (next_x < 0 || next_x >= crop_width ||
                            (dx == 0 && dy == 0))
                        {
                            continue;
                        }
                        next_index = (uint32_t)(next_y * crop_width + next_x);
                        if (mask[next_index] == 1)
                        {
                            mask[next_index] = 2;
                            queue[tail++] = next_index;
                        }
                    }
                }
            }

            filled_area = 0.0;
            filled_m10 = 0.0;
            filled_m01 = 0.0;
            silhouette_perimeter = 0.0;
            for (dy = component_min_y; dy <= component_max_y; dy++)
            {
                int left;
                int right;
                int previous_left;
                int previous_right;
                int current_width;
                int overlap;

                left = row_min[dy];
                right = row_max[dy];
                if (right < left)
                {
                    continue;
                }
                current_width = right - left + 1;
                filled_area += (double)current_width;
                filled_m10 += ((double)left + (double)right) *
                              (double)current_width / 2.0;
                filled_m01 += (double)dy * (double)current_width;
                silhouette_perimeter += 2.0;
                if (dy == component_min_y)
                {
                    silhouette_perimeter += (double)current_width;
                }
                else
                {
                    previous_left = row_min[dy - 1];
                    previous_right = row_max[dy - 1];
                    if (previous_right < previous_left)
                    {
                        silhouette_perimeter += (double)current_width;
                    }
                    else
                    {
                        overlap = (right < previous_right ? right : previous_right) -
                                  (left > previous_left ? left : previous_left) + 1;
                        if (overlap < 0)
                        {
                            overlap = 0;
                        }
                        silhouette_perimeter += (double)(current_width +
                            previous_right - previous_left + 1 - 2 * overlap);
                    }
                }
                if (dy == component_max_y)
                {
                    silhouette_perimeter += (double)current_width;
                }
            }
            bbox_area = (double)(component_max_x - component_min_x + 1) *
                        (double)(component_max_y - component_min_y + 1);
            fill_ratio = bbox_area > 0.0 ? filled_area / bbox_area : 0.0;
            circularity = silhouette_perimeter > 0.0 ?
                4.0 * PVT_PI * filled_area /
                (silhouette_perimeter * silhouette_perimeter) : 0.0;
            distance = fabs(crop_area - bbox_area);
            if (bbox_area >= crop_area * 0.20 &&
                fill_ratio >= 0.25 && circularity >= 0.20 &&
                (!best_found || distance < best_distance))
            {
                best_found = 1;
                best_distance = distance;
                best_area = filled_area;
                best_m10 = filled_m10;
                best_m01 = filled_m01;
                best_perimeter = silhouette_perimeter;
            }
        }
    }
    if (!best_found || best_area <= 0.0 || best_perimeter <= 0.0)
    {
        double est_r;
        double box_cx;
        double box_cy;
        double r_limit2;
        double dark_area = 0.0;
        double dark_m10 = 0.0;
        double dark_m01 = 0.0;

        est_r = 0.25 * ((double)box->x2 - (double)box->x1 +
                        (double)box->y2 - (double)box->y1);
        box_cx = 0.5 * ((double)box->x1 + (double)box->x2);
        box_cy = 0.5 * ((double)box->y1 + (double)box->y2);
        r_limit2 = (est_r * 1.15) * (est_r * 1.15);
        for (y = 0; y < crop_height; y++)
        {
            for (x = 0; x < crop_width; x++)
            {
                if (mask[y * crop_width + x] != 0)
                {
                    double dx;
                    double dy;

                    dx = (double)x + 0.5 - box_cx + (double)crop_x1;
                    dy = (double)y + 0.5 - box_cy + (double)crop_y1;
                    if (dx * dx + dy * dy <= r_limit2)
                    {
                        dark_area += 1.0;
                        dark_m10 += (double)x + 0.5;
                        dark_m01 += (double)y + 0.5;
                    }
                }
            }
        }
        if (dark_area >= 0.10 * PVT_PI * est_r * est_r)
        {
            double cx_union;
            double cy_union;
            double radius_union;
            double distance;

            best_found = 1;
            used_fallback = 1;
            best_area = dark_area;
            best_m10 = dark_m10;
            best_m01 = dark_m01;
            best_perimeter = 2.0 * PVT_PI * est_r;
            cx_union = (dark_m10 / dark_area) + (double)crop_x1;
            cy_union = (dark_m01 / dark_area) + (double)crop_y1;
            radius_union = fmax(sqrt(2.0 * dark_area / PVT_PI), 0.80 * est_r);
            distance = hypot(cx_union - box_cx, cy_union - box_cy);
            if (distance > 0.35 * est_r)
            {
                best_m10 = (double)box_cx - (double)crop_x1;
                best_m01 = (double)box_cy - (double)crop_y1;
                best_area = PVT_PI * est_r * est_r;
                best_perimeter = 2.0 * PVT_PI * est_r;
                radius_union = (double)est_r;
            }
            else
            {
                best_m10 = dark_m10;
                best_m01 = dark_m01;
                best_area = dark_area;
                best_perimeter = 2.0 * PVT_PI * est_r;
                best_found = 1;
                if (radius_union > est_r)
                {
                    best_perimeter = 2.0 * PVT_PI * radius_union;
                }
            }
        }
    }
    if (!best_found || best_area <= 0.0 || best_perimeter <= 0.0)
    {
        pvt_fail(out, "no_candidate");
        return 0;
    }
    {
        float center_x;
        float center_y;
        float radius;
        float refined_cx;
        float refined_cy;
        float r_eq;
        pvt_seam_lines_t seam_lines;
        double est_r;
        double box_cx;
        double box_cy;
        double dark_count;
        double sector_means[4];
        int profile;
        double contrast_sum;
        double overshoot_sum;
        double undershoot_sum;

        est_r = 0.25 * ((double)box->x2 - (double)box->x1 +
                        (double)box->y2 - (double)box->y1);
        box_cx = 0.5 * ((double)box->x1 + (double)box->x2);
        box_cy = 0.5 * ((double)box->y1 + (double)box->y2);

        center_x = (float)(best_m10 / best_area + (double)crop_x1);
        center_y = (float)(best_m01 / best_area + (double)crop_y1);
        radius = (float)sqrt((used_fallback ? 2.0 : 1.0) * best_area / PVT_PI);
        if (used_fallback == 1 &&
            hypot((double)center_x - box_cx, (double)center_y - box_cy) >
            0.35 * est_r)
        {
            center_x = (float)box_cx;
            center_y = (float)box_cy;
            radius = (float)est_r;
        }
        if (!pvt_set_means(bgra, width, height, stride, center_x, center_y,
                           radius, sector_means))
        {
            pvt_fail(out, "quadrant_mismatch");
            return 0;
        }
        if (pvt_refine_center_knife_edge(bgra, width, height, stride,
                                         center_x, center_y, radius,
                                         &refined_cx, &refined_cy,
                                         &seam_lines))
        {
            center_x = refined_cx;
            center_y = refined_cy;
            out->lines_valid = 1;
            out->line_v_slope = (float)seam_lines.a_v;
            out->line_v_offset = (float)seam_lines.b_v;
            out->line_h_slope = (float)seam_lines.a_h;
            out->line_h_offset = (float)seam_lines.b_h;
        }
        else
        {
            out->lines_valid = 0;
            out->line_v_slope = 0.0f;
            out->line_v_offset = 0.0f;
            out->line_h_slope = 0.0f;
            out->line_h_offset = 0.0f;
        }
        if (used_fallback == 0 && best_area >= 0.70 * PVT_PI * est_r * est_r)
        {
            r_eq = (float)sqrt(best_area / PVT_PI);
        }
        else
        {
            double search_radius;
            double dark_sum;

            search_radius = 1.15 * est_r;
            dark_sum = 0.0;
            for (y = 0; y < crop_height; y++)
            {
                for (x = 0; x < crop_width; x++)
                {
                    double dx;
                    double dy;

                    dx = ((double)x + 0.5) -
                         ((double)center_x - (double)crop_x1);
                    dy = ((double)y + 0.5) -
                         ((double)center_y - (double)crop_y1);
                    if (mask[y * crop_width + x] != 0 &&
                        dx * dx + dy * dy <= search_radius * search_radius)
                    {
                        dark_sum += 1.0;
                    }
                }
            }
            dark_count = dark_sum;
            r_eq = (float)fmin(1.15 * est_r,
                               fmax(0.75 * est_r,
                                    sqrt(2.0 * dark_count / PVT_PI)));
        }
        if (!pvt_set_means(bgra, width, height, stride, center_x, center_y,
                           r_eq, sector_means))
        {
            pvt_fail(out, "quadrant_mismatch");
            return 0;
        }
        out->cx = center_x;
        out->cy = center_y;
        out->r_eq = r_eq;
        contrast_sum = 0.0;
        overshoot_sum = 0.0;
        undershoot_sum = 0.0;
        out->contrast_min = 1.0f;
        for (profile = 0; profile < 4; profile++)
        {
            double samples[64];
            double contrast;
            double overshoot;
            double undershoot;
            double span;
            int sample_count;
            int sample;

            span = 1.2 * (double)r_eq;
            sample_count = (int)ceil(span) + 1;
            if (sample_count < 10)
            {
                sample_count = 10;
            }
            if (sample_count > 64)
            {
                sample_count = 64;
            }
            out->profile_len[profile] = sample_count;
            for (sample = 0; sample < sample_count; sample++)
            {
                double offset_along;
                double sample_x;
                double sample_y;
                int image_x;
                int image_y;
                int luminance;

                offset_along = -0.6 * (double)r_eq +
                    span * (double)sample / (double)(sample_count - 1);
                if (profile == 0)
                {
                    sample_x = (double)center_x + offset_along;
                    sample_y = (double)center_y - 0.5 * (double)r_eq;
                }
                else if (profile == 1)
                {
                    sample_x = (double)center_x + offset_along;
                    sample_y = (double)center_y + 0.5 * (double)r_eq;
                }
                else if (profile == 2)
                {
                    sample_x = (double)center_x - 0.5 * (double)r_eq;
                    sample_y = (double)center_y + offset_along;
                }
                else
                {
                    sample_x = (double)center_x + 0.5 * (double)r_eq;
                    sample_y = (double)center_y + offset_along;
                }
                image_x = (int)floor(sample_x + 0.5);
                image_y = (int)floor(sample_y + 0.5);
                if (image_x < 0 || image_x >= width ||
                    image_y < 0 || image_y >= height)
                {
                    pvt_fail(out, "profile_incomplete");
                    return 0;
                }
                luminance = pvt_luminance(
                    bgra + (size_t)image_y * (size_t)stride +
                    (size_t)image_x * 4U);
                out->profiles[profile][sample] = (float)luminance;
                samples[sample] = (double)luminance;
            }
            if (!Pvt_AnalyzeProfile(samples, sample_count, &contrast,
                                    &overshoot, &undershoot))
            {
                pvt_fail(out, "profile_incomplete");
                return 0;
            }
            contrast_sum += contrast;
            overshoot_sum += overshoot;
            undershoot_sum += undershoot;
            if ((float)contrast < out->contrast_min)
            {
                out->contrast_min = (float)contrast;
            }
            if ((float)overshoot > out->overshoot_max)
            {
                out->overshoot_max = (float)overshoot;
            }
            if ((float)undershoot > out->undershoot_max)
            {
                out->undershoot_max = (float)undershoot;
            }
        }
        out->contrast_avg = (float)(contrast_sum / 4.0);
        out->overshoot_avg = (float)(overshoot_sum / 4.0);
        out->undershoot_avg = (float)(undershoot_sum / 4.0);
        out->used = 1;
        out->reason = NULL;
    }
    free(mask);
    free(queue);
    return 1;
}
