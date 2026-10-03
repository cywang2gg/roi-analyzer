#include "cc_locate.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc_cv.h"

typedef struct {
    float position;
    int axis;
} cc_position_t;

static int cc_valid_view(cc_image_view_t image)
{
    int bytes_per_pixel;

    if (image.channels != 3 && image.channels != 4) {
        return 0;
    }
    bytes_per_pixel = image.channels;
    if (image.pixels == NULL || image.width <= 0 || image.height <= 0 ||
        image.width > INT_MAX / bytes_per_pixel ||
        image.stride < image.width * bytes_per_pixel ||
        image.stride <= 0) {
        return 0;
    }
    return (size_t)image.height <= SIZE_MAX / (size_t)image.stride;
}

static int cc_valid_box(cc_box_t box)
{
    return isfinite(box.xc) && isfinite(box.yc) &&
           isfinite(box.width) && isfinite(box.height) &&
           box.width > 0.0f && box.height > 0.0f;
}

static int cc_clamp_int(double value, int low, int high)
{
    if (value < (double)low) {
        return low;
    }
    if (value > (double)high) {
        return high;
    }
    return (int)value;
}

static int cc_truncate_coord(double value, int *result)
{
    if (result == NULL || isnan(value)) {
        return -1;
    }
    if (value <= (double)INT_MIN) {
        *result = INT_MIN;
    } else if (value >= (double)INT_MAX) {
        *result = INT_MAX;
    } else {
        *result = (int)value;
    }
    return 0;
}

static uint8_t cc_round_byte(double value)
{
    if (value < 0.0) {
        value = 0.0;
    }
    if (value > 255.0) {
        value = 255.0;
    }
    return (uint8_t)floor(value + 0.5);
}

static int cc_float_compare(const void *left_pointer,
                            const void *right_pointer)
{
    const float left = *(const float *)left_pointer;
    const float right = *(const float *)right_pointer;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static float cc_median(const float *values, size_t count)
{
    float *sorted;
    float median;

    sorted = (float *)malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        return -1.0f;
    }
    memcpy(sorted, values, count * sizeof(*sorted));
    qsort(sorted, count, sizeof(*sorted), cc_float_compare);
    if ((count & 1U) != 0U) {
        median = sorted[count / 2U];
    } else {
        median = (sorted[count / 2U - 1U] + sorted[count / 2U]) * 0.5f;
    }
    free(sorted);
    return median;
}

static size_t cc_python_round(float value)
{
    const float lower = floorf(value);
    const float fraction = value - lower;

    if (fraction < 0.5f) {
        return (size_t)lower;
    }
    if (fraction > 0.5f) {
        return (size_t)(lower + 1.0f);
    }
    if (((size_t)lower & 1U) == 0U) {
        return (size_t)lower;
    }
    return (size_t)(lower + 1.0f);
}

static int cc_refine_sorted(const float *positions, size_t position_count,
                            int dim, size_t target_count, float spacing,
                            float *refined, size_t refined_capacity,
                            size_t *refined_count)
{
    size_t count = 0;
    size_t i;
    size_t capacity;
    float *work;

    *refined_count = 0;
    if (position_count == 0) {
        return 0;
    }
    capacity = position_count + target_count;
    if (capacity < position_count || capacity == SIZE_MAX) {
        return -1;
    }
    ++capacity;
    for (i = 1; i < position_count; ++i) {
        const float gap = positions[i] - positions[i - 1U];
        if (spacing > 0.0f && gap > 1.5f * spacing) {
            const size_t rounded = cc_python_round(gap / spacing);
            if (rounded > 1U) {
                if (rounded - 1U > SIZE_MAX - capacity) {
                    return -1;
                }
                capacity += rounded - 1U;
            }
        }
    }
    if (capacity > SIZE_MAX / sizeof(*work)) {
        return -1;
    }
    work = (float *)malloc(capacity * sizeof(*work));
    if (work == NULL) {
        return -1;
    }
    work[count++] = positions[0];
    for (i = 1; i < position_count; ++i) {
        const float gap = positions[i] - positions[i - 1U];
        size_t inserts = 0;
        size_t j;

        if (spacing > 0.0f && gap > 1.5f * spacing) {
            const size_t rounded = cc_python_round(gap / spacing);
            if (rounded > 1U) {
                inserts = rounded - 1U;
            }
        }
        if (inserts > SIZE_MAX - count - 1U || count + inserts + 1U > capacity) {
            free(work);
            return -1;
        }
        for (j = 1; j <= inserts; ++j) {
            work[count++] = positions[i - 1U] +
                            gap * (float)j / (float)(inserts + 1U);
        }
        work[count++] = positions[i];
    }

    if (count < target_count) {
        float mean_spacing = spacing;
        if (count > 1U) {
            double sum = 0.0;
            for (i = 1; i < count; ++i) {
                sum += (double)work[i] - (double)work[i - 1U];
            }
            mean_spacing = (float)(sum / (double)(count - 1U));
        }
        if (!isfinite(mean_spacing) || mean_spacing <= 0.0f) {
            free(work);
            return -1;
        }
        while (count < target_count) {
            const float left_gap = work[0];
            const float right_gap = (float)dim - work[count - 1U];

            if (left_gap > right_gap) {
                memmove(work + 1, work, count * sizeof(*work));
                work[0] -= mean_spacing;
            } else {
                work[count] = work[count - 1U] + mean_spacing;
            }
            ++count;
        }
    }
    if (count > target_count) {
        const float center = (work[0] + work[count - 1U]) * 0.5f;
        while (count > target_count) {
            if (center - work[0] > work[count - 1U] - center) {
                memmove(work, work + 1, (count - 1U) * sizeof(*work));
            }
            --count;
        }
    }
    if (count > refined_capacity) {
        free(work);
        return -1;
    }
    memcpy(refined, work, count * sizeof(*refined));
    *refined_count = count;
    free(work);
    return 0;
}

static int cc_axis_quality(const float *positions, size_t count, int dim,
                           size_t target_count, float *typical, int *is_good)
{
    float *sorted;
    float *differences;
    size_t i;

    *typical = (float)dim / (float)target_count;
    *is_good = 0;
    if (count < 2U) {
        return 0;
    }
    if (count > SIZE_MAX / sizeof(*sorted)) {
        return -1;
    }
    sorted = (float *)malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        return -1;
    }
    memcpy(sorted, positions, count * sizeof(*sorted));
    for (i = 0; i < count; ++i) {
        if (!isfinite(sorted[i])) {
            free(sorted);
            return -1;
        }
    }
    qsort(sorted, count, sizeof(*sorted), cc_float_compare);
    differences = (float *)malloc((count - 1U) * sizeof(*differences));
    if (differences == NULL) {
        free(sorted);
        return -1;
    }
    for (i = 1; i < count; ++i) {
        differences[i - 1U] = sorted[i] - sorted[i - 1U];
    }
    *typical = cc_median(differences, count - 1U);
    free(differences);
    if (*typical < 0.0f) {
        free(sorted);
        return -1;
    }
    if (*typical <= 5.0f) {
        *typical = (float)dim / (float)target_count;
    }
    *is_good = (sorted[count - 1U] - sorted[0] >
                0.5f * (float)dim) && count > 2U;
    free(sorted);
    return 0;
}

int cc_crop_scale(cc_image_view_t source, cc_box_t box,
                  float scale_factor, int max_dimension,
                  cc_image_t *cropped, int *origin_x, int *origin_y,
                  float *rx, float *ry)
{
    double half_width;
    double half_height;
    int x1;
    int y1;
    int x2;
    int y2;
    int crop_width;
    int crop_height;
    int output_width;
    int output_height;
    int y;
    size_t output_bytes;

    if (!cc_valid_view(source) || source.channels != 4 ||
        !cc_valid_box(box) ||
        !isfinite(scale_factor) || scale_factor <= 0.0f ||
        max_dimension <= 0 || cropped == NULL || origin_x == NULL ||
        origin_y == NULL || rx == NULL || ry == NULL) {
        return -1;
    }
    memset(cropped, 0, sizeof(*cropped));
    half_width = (double)box.width * (double)scale_factor * 0.5;
    half_height = (double)box.height * (double)scale_factor * 0.5;
    x1 = cc_clamp_int((double)box.xc - half_width, 0, source.width - 1);
    y1 = cc_clamp_int((double)box.yc - half_height, 0, source.height - 1);
    x2 = cc_clamp_int((double)box.xc + half_width, 0, source.width);
    y2 = cc_clamp_int((double)box.yc + half_height, 0, source.height);
    crop_width = x2 - x1;
    crop_height = y2 - y1;
    if (crop_width <= 0 || crop_height <= 0) {
        return -1;
    }
    output_width = crop_width;
    output_height = crop_height;
    if (crop_width > max_dimension || crop_height > max_dimension) {
        const double scale = fmin((double)max_dimension / crop_width,
                                  (double)max_dimension / crop_height);
        output_width = (int)((double)crop_width * scale);
        output_height = (int)((double)crop_height * scale);
        if (output_width < 1) {
            output_width = 1;
        }
        if (output_height < 1) {
            output_height = 1;
        }
    }
    if (output_width > INT_MAX / 3 ||
        (size_t)output_width > SIZE_MAX / 3U / (size_t)output_height) {
        return -1;
    }
    output_bytes = (size_t)output_width * (size_t)output_height * 3U;
    cropped->pixels = (uint8_t *)malloc(output_bytes);
    if (cropped->pixels == NULL) {
        return -1;
    }
    cropped->width = output_width;
    cropped->height = output_height;
    cropped->stride = output_width * 3;
    cropped->channels = 3;

    for (y = 0; y < output_height; ++y) {
        const double sy0 = (double)y * (double)crop_height / output_height;
        const double sy1 = (double)(y + 1) * (double)crop_height /
                           output_height;
        const int iy0 = (int)floor(sy0);
        const int iy1 = (int)ceil(sy1);
        int x;

        for (x = 0; x < output_width; ++x) {
            const double sx0 = (double)x * (double)crop_width / output_width;
            const double sx1 = (double)(x + 1) * (double)crop_width /
                               output_width;
            const int ix0 = (int)floor(sx0);
            const int ix1 = (int)ceil(sx1);
            int channel;

            for (channel = 0; channel < 3; ++channel) {
                double weighted_sum = 0.0;
                double total_weight = 0.0;
                int source_y;

                for (source_y = iy0; source_y < iy1; ++source_y) {
                    const double wy = fmin(sy1, (double)(source_y + 1)) -
                                      fmax(sy0, (double)source_y);
                    const uint8_t *row = source.pixels +
                        (size_t)(y1 + source_y) * (size_t)source.stride;
                    int source_x;

                    for (source_x = ix0; source_x < ix1; ++source_x) {
                        const double wx =
                            fmin(sx1, (double)(source_x + 1)) -
                            fmax(sx0, (double)source_x);
                        const double weight = wx * wy;
                        weighted_sum += (double)row[
                            (size_t)(x1 + source_x) * 4U +
                            (size_t)channel] * weight;
                        total_weight += weight;
                    }
                }
                cropped->pixels[(size_t)y * (size_t)cropped->stride +
                                (size_t)x * 3U + (size_t)channel] =
                    cc_round_byte(weighted_sum / total_weight);
            }
        }
    }

    *origin_x = x1;
    *origin_y = y1;
    *rx = (float)output_width / (float)crop_width;
    *ry = (float)output_height / (float)crop_height;
    return 0;
}

void cc_image_free(cc_image_t *image)
{
    if (image == NULL) {
        return;
    }
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

int cc_merge_positions(const cc_segment_t *segments, size_t segment_count,
                       float spacing_threshold, float *horizontal,
                       size_t horizontal_capacity, size_t *horizontal_count,
                       float *vertical, size_t vertical_capacity,
                       size_t *vertical_count)
{
    cc_position_t *positions;
    size_t count = 0;
    size_t i;
    size_t h_count = 0;
    size_t v_count = 0;
    int axis;

    if (horizontal_count == NULL || vertical_count == NULL ||
        (segment_count > 0U && segments == NULL) ||
        (horizontal_capacity > 0U && horizontal == NULL) ||
        (vertical_capacity > 0U && vertical == NULL) ||
        !isfinite(spacing_threshold) || spacing_threshold <= 0.0f ||
        segment_count > SIZE_MAX / sizeof(*positions)) {
        return -1;
    }
    *horizontal_count = 0;
    *vertical_count = 0;
    positions = (cc_position_t *)malloc(segment_count * sizeof(*positions));
    if (segment_count != 0U && positions == NULL) {
        return -1;
    }
    for (i = 0; i < segment_count; ++i) {
        const double dx = (double)segments[i].x2 - segments[i].x1;
        const double dy = (double)segments[i].y2 - segments[i].y1;
        const double angle = atan2(dy, dx) * 180.0 / 3.14159265358979323846;
        const double abs_angle = fabs(angle);

        if (abs_angle < 15.0 || fabs(abs_angle - 180.0) < 15.0) {
            positions[count].position =
                ((float)segments[i].y1 + (float)segments[i].y2) * 0.5f;
            positions[count++].axis = 0;
        } else if (fabs(abs_angle - 90.0) < 15.0) {
            positions[count].position =
                ((float)segments[i].x1 + (float)segments[i].x2) * 0.5f;
            positions[count++].axis = 1;
        }
    }
    for (axis = 0; axis < 2; ++axis) {
        float *axis_positions;
        size_t axis_count = 0;
        size_t cluster_count = 0;
        double cluster_sum = 0.0;
        float *output = axis == 0 ? horizontal : vertical;
        size_t output_capacity = axis == 0 ? horizontal_capacity :
                                             vertical_capacity;
        size_t *output_count = axis == 0 ? &h_count : &v_count;

        axis_positions = (float *)malloc(count * sizeof(*axis_positions));
        if (count != 0U && axis_positions == NULL) {
            free(positions);
            return -1;
        }
        for (i = 0; i < count; ++i) {
            if (positions[i].axis == axis) {
                axis_positions[axis_count++] = positions[i].position;
            }
        }
        if (axis_count > 1U) {
            qsort(axis_positions, axis_count, sizeof(*axis_positions),
                  cc_float_compare);
        }
        for (i = 0; i <= axis_count; ++i) {
            if (cluster_count != 0U &&
                (i == axis_count ||
                 fabs((double)axis_positions[i] -
                      cluster_sum / (double)cluster_count) >=
                     (double)spacing_threshold)) {
                if (*output_count >= output_capacity) {
                    free(axis_positions);
                    free(positions);
                    return -1;
                }
                output[(*output_count)++] =
                    (float)(cluster_sum / (double)cluster_count);
                cluster_sum = 0.0;
                cluster_count = 0;
            }
            if (i < axis_count) {
                cluster_sum += axis_positions[i];
                ++cluster_count;
            }
        }
        free(axis_positions);
    }
    free(positions);
    *horizontal_count = h_count;
    *vertical_count = v_count;
    return 0;
}

int cc_refine_axis(const float *positions, size_t position_count, int dim,
                   size_t target_count, float forced_spacing,
                   int has_forced_spacing, float *refined,
                   size_t refined_capacity, size_t *refined_count,
                   float *typical_spacing, int *is_good)
{
    float *sorted;
    float typical;
    int good;
    size_t i;
    int result;

    if (refined_count == NULL || typical_spacing == NULL || is_good == NULL ||
        (position_count > 0U && positions == NULL) || refined == NULL ||
        dim <= 0 || target_count == 0U ||
        refined_capacity < target_count ||
        (has_forced_spacing && (!isfinite(forced_spacing) ||
                                forced_spacing <= 0.0f))) {
        return -1;
    }
    *refined_count = 0;
    if (position_count == 0U) {
        *typical_spacing = (float)dim / (float)target_count;
        *is_good = 0;
        return 0;
    }
    if (position_count > SIZE_MAX / sizeof(*sorted)) {
        return -1;
    }
    sorted = (float *)malloc(position_count * sizeof(*sorted));
    if (sorted == NULL) {
        return -1;
    }
    for (i = 0; i < position_count; ++i) {
        if (!isfinite(positions[i])) {
            free(sorted);
            return -1;
        }
        sorted[i] = positions[i];
    }
    qsort(sorted, position_count, sizeof(*sorted), cc_float_compare);
    result = cc_axis_quality(sorted, position_count, dim, target_count,
                             &typical, &good);
    if (result != 0) {
        free(sorted);
        return -1;
    }
    *typical_spacing = typical;
    *is_good = good;
    if (!good && has_forced_spacing) {
        typical = forced_spacing;
    }
    result = cc_refine_sorted(sorted, position_count, dim, target_count,
                              typical, refined, refined_capacity,
                              refined_count);
    free(sorted);
    return result;
}

int cc_grid_refine(const float *horizontal, size_t horizontal_count,
                   const float *vertical, size_t vertical_count,
                   int width, int height, cc_grid_t *grid)
{
    float h_typical;
    float v_typical;
    float h_values[CC_MAX_H_LINES];
    float v_values[CC_MAX_V_LINES];
    size_t h_count = 0;
    size_t v_count = 0;
    size_t h_target;
    size_t v_target;
    int h_good = 0;
    int v_good = 0;
    int swapped = 0;

    if (grid == NULL || width <= 0 || height <= 0 ||
        (horizontal_count > 0U && horizontal == NULL) ||
        (vertical_count > 0U && vertical == NULL)) {
        return -1;
    }
    memset(grid, 0, sizeof(*grid));
    if (horizontal_count == 0U && vertical_count == 0U) {
        grid->reason = "no_lines";
        return 1;
    }
    if (llabs((long long)horizontal_count - CC_H_LINES_LAND) +
            llabs((long long)vertical_count - CC_V_LINES_LAND) >
        llabs((long long)horizontal_count - CC_H_LINES_PORT) +
            llabs((long long)vertical_count - CC_V_LINES_PORT)) {
        swapped = 1;
    }
    if (swapped) {
        h_target = CC_H_LINES_PORT;
        v_target = CC_V_LINES_PORT;
    } else {
        h_target = CC_H_LINES_LAND;
        v_target = CC_V_LINES_LAND;
    }
    /* dim never follows the swap: H axis dim is always height (root-cause C) */
    if (cc_axis_quality(horizontal, horizontal_count, height,
                        h_target, &h_typical, &h_good) != 0 ||
        cc_axis_quality(vertical, vertical_count, width,
                        v_target, &v_typical, &v_good) != 0) {
        return -1;
    }
    if (cc_refine_axis(horizontal, horizontal_count, height,
                       h_target, v_typical, v_good,
                       h_values, CC_MAX_H_LINES, &h_count,
                       &h_typical, &h_good) != 0 ||
        cc_refine_axis(vertical, vertical_count, width,
                       v_target, h_typical, h_good,
                       v_values, CC_MAX_V_LINES, &v_count,
                       &v_typical, &v_good) != 0) {
        return -1;
    }
    grid->swapped = swapped;
    grid->h_target = h_target;
    grid->v_target = v_target;
    grid->h_good = h_good;
    grid->v_good = v_good;
    memcpy(grid->h, h_values, h_count * sizeof(*grid->h));
    memcpy(grid->v, v_values, v_count * sizeof(*grid->v));
    grid->h_count = h_count;
    grid->v_count = v_count;
    if (h_count != h_target || v_count != v_target) {
        grid->reason = "no_grid";
        return 1;
    }
    return 0;
}

/* rot: 0=upright, 1=cw90, 2=180, 3=ccw90. rows/cols are image grid dims. */
int cc_canon_id(int img_row, int img_col, int rows, int cols, int rot)
{
    int r;
    int c;

    switch (rot) {
    case 1:
        r = 3 - img_col;
        c = img_row;
        break;
    case 2:
        r = 3 - img_row;
        c = 5 - img_col;
        break;
    case 3:
        r = img_col;
        c = 5 - img_row;
        break;
    default:
        r = img_row;
        c = img_col;
        break;
    }
    (void)rows;
    (void)cols;
    if (r < 0 || r > 3 || c < 0 || c > 5) {
        return -1;
    }
    return r * 6 + c + 1;
}

int cc_np_slice(int start, int stop, int len, int *out_start)
{
    if (len < 0 || out_start == NULL) {
        return -1;
    }
    if (start < 0) {
        start += len;
    }
    if (stop < 0) {
        stop += len;
    }
    if (start < 0) {
        start = 0;
    }
    if (stop < 0) {
        stop = 0;
    }
    if (start > len) {
        start = len;
    }
    if (stop > len) {
        stop = len;
    }
    *out_start = start;
    return stop - start;
}

int cc_extract_rois(const float *horizontal, size_t horizontal_count,
                    const float *vertical, size_t vertical_count,
                    int image_width, int image_height,
                    int origin_x, int origin_y, float rx, float ry,
                    cc_roi_t rois[CC_ROI_COUNT], size_t *valid_count)
{
    size_t valid = 0;
    size_t i;
    size_t rows;
    size_t cols;
    size_t row;

    if (horizontal == NULL || vertical == NULL || rois == NULL ||
        valid_count == NULL || image_width <= 0 || image_height <= 0 ||
        !isfinite(rx) || !isfinite(ry) || rx <= 0.0f || ry <= 0.0f) {
        return -1;
    }
    /* grid dims: landscape 5x7 -> 4x6 cells; portrait 7x5 -> 6x4 cells */
    if (horizontal_count == CC_H_LINES_LAND &&
        vertical_count == CC_V_LINES_LAND) {
        rows = 4U;
        cols = 6U;
    } else if (horizontal_count == CC_H_LINES_PORT &&
               vertical_count == CC_V_LINES_PORT) {
        rows = 6U;
        cols = 4U;
    } else {
        return -1;
    }
    *valid_count = 0;
    memset(rois, 0, CC_ROI_COUNT * sizeof(*rois));
    for (i = 0; i < horizontal_count; ++i) {
        if (!isfinite(horizontal[i])) {
            return -1;
        }
    }
    for (i = 0; i < vertical_count; ++i) {
        if (!isfinite(vertical[i])) {
            return -1;
        }
    }
    for (row = 0; row < rows; ++row) {
        size_t column;
        for (column = 0; column < cols; ++column) {
            const int id = (int)(row * cols + column + 1U);
            const double left = vertical[column];
            const double right = vertical[column + 1];
            const double top = horizontal[row];
            const double bottom = horizontal[row + 1];
            int ix1;
            int ix2;
            int iy1;
            int iy2;
            int actual_x1;
            int actual_y1;
            int width;
            int height;
            cc_roi_t *roi = &rois[id - 1];

            roi->id = id;
            if (cc_truncate_coord(
                    left + (right - left) * (double)CC_INSET_RATIO,
                    &ix1) != 0 ||
                cc_truncate_coord(
                    right - (right - left) * (double)CC_INSET_RATIO,
                    &ix2) != 0 ||
                cc_truncate_coord(
                    top + (bottom - top) * (double)CC_INSET_RATIO,
                    &iy1) != 0 ||
                cc_truncate_coord(
                    bottom - (bottom - top) * (double)CC_INSET_RATIO,
                    &iy2) != 0) {
                return -1;
            }
            width = cc_np_slice(ix1, ix2, image_width, &actual_x1);
            height = cc_np_slice(iy1, iy2, image_height, &actual_y1);
            if (width <= 0 || height <= 0) {
                continue;
            }
            roi->x1 = actual_x1;
            roi->y1 = actual_y1;
            roi->x2 = actual_x1 + width;
            roi->y2 = actual_y1 + height;
            roi->valid = 1;
            roi->display_x1 = (float)roi->x1 / rx + (float)origin_x;
            roi->display_y1 = (float)roi->y1 / ry + (float)origin_y;
            roi->display_x2 = (float)roi->x2 / rx + (float)origin_x;
            roi->display_y2 = (float)roi->y2 / ry + (float)origin_y;
            ++valid;
        }
    }
    *valid_count = valid;
    return 0;
}

int cc_roi_stats(const cc_image_view_t image, const cc_roi_t *roi,
                 cc_roi_stats_t *stats)
{
    double sum_b = 0.0;
    double sum_g = 0.0;
    double sum_r = 0.0;
    double sum_y = 0.0;
    double sum_y_squared = 0.0;
    size_t pixel_count;
    int y;

    if (!cc_valid_view(image) || roi == NULL || stats == NULL ||
        !roi->valid || roi->x1 < 0 || roi->y1 < 0 ||
        roi->x2 > image.width || roi->y2 > image.height ||
        roi->x2 <= roi->x1 || roi->y2 <= roi->y1) {
        return -1;
    }
    memset(stats, 0, sizeof(*stats));
    pixel_count = (size_t)(roi->x2 - roi->x1) *
                  (size_t)(roi->y2 - roi->y1);
    if (pixel_count == 0U) {
        return -1;
    }
    for (y = roi->y1; y < roi->y2; ++y) {
        const uint8_t *row = image.pixels +
            (size_t)y * (size_t)image.stride +
            (size_t)roi->x1 * (size_t)image.channels;
        int x;

        for (x = roi->x1; x < roi->x2; ++x) {
            const double b = row[(size_t)(x - roi->x1) *
                                 (size_t)image.channels];
            const double g = row[(size_t)(x - roi->x1) *
                                 (size_t)image.channels + 1U];
            const double r = row[(size_t)(x - roi->x1) *
                                 (size_t)image.channels + 2U];
            const double luminance = 0.114 * b + 0.587 * g + 0.299 * r;
            sum_b += b;
            sum_g += g;
            sum_r += r;
            sum_y += luminance;
            sum_y_squared += luminance * luminance;
        }
    }
    stats->mean_b = sum_b / (double)pixel_count;
    stats->mean_g = sum_g / (double)pixel_count;
    stats->mean_r = sum_r / (double)pixel_count;
    stats->y_mean = sum_y / (double)pixel_count;
    stats->y_std = sqrt(fmax(0.0, sum_y_squared / (double)pixel_count -
                                    stats->y_mean * stats->y_mean));
    return 0;
}

int cc_locate_run(cc_image_view_t image, int origin_x, int origin_y,
                  float rx, float ry, cc_locate_result_t *result)
{
    cc_image_t edge;
    cc_segment_t segments[CC_HOUGH_MAX_SEGMENTS];
    float horizontal[CC_HOUGH_MAX_SEGMENTS];
    float vertical[CC_HOUGH_MAX_SEGMENTS];
    size_t segment_count = 0;
    size_t horizontal_count = 0;
    size_t vertical_count = 0;
    size_t i;
    int status;

    if (result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    memset(&edge, 0, sizeof(edge));
    if (!cc_valid_view(image) || image.channels != 3 ||
        !isfinite(rx) || !isfinite(ry) || rx <= 0.0f || ry <= 0.0f) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "invalid_image");
        return -1;
    }
    if (cc_cv_api() == NULL) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "cv_unavailable");
        return -1;
    }
    if (cc_gray_blur_canny(image, &edge) != 0) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "edge_failed");
        return -1;
    }
    {
        cc_image_view_t edge_view;
        edge_view.pixels = edge.pixels;
        edge_view.width = edge.width;
        edge_view.height = edge.height;
        edge_view.stride = edge.stride;
        edge_view.channels = edge.channels;
        status = cc_hough_lines_p(edge_view, segments,
                                  CC_HOUGH_MAX_SEGMENTS, &segment_count);
    }
    cc_image_free(&edge);
    if (status != 0) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "hough_failed");
        return -1;
    }
    if (cc_merge_positions(segments, segment_count, 25.0f,
                           horizontal, CC_HOUGH_MAX_SEGMENTS,
                           &horizontal_count, vertical,
                           CC_HOUGH_MAX_SEGMENTS, &vertical_count) != 0) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "merge_failed");
        return -1;
    }
    status = cc_grid_refine(horizontal, horizontal_count, vertical,
                            vertical_count, image.width, image.height,
                            &result->grid);
    if (status != 0) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       result->grid.reason != NULL ?
                       result->grid.reason : "no_grid");
        return 1;
    }
    if (cc_extract_rois(result->grid.h, result->grid.h_count,
                        result->grid.v, result->grid.v_count,
                        image.width, image.height, origin_x, origin_y, rx, ry,
                        result->rois, &result->valid_count) != 0) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "roi_failed");
        return -1;
    }
    for (i = 0; i < CC_ROI_COUNT; ++i) {
        if (result->rois[i].valid &&
            cc_roi_stats(image, &result->rois[i], &result->stats[i]) != 0) {
            (void)snprintf(result->reason, sizeof(result->reason), "%s",
                           "stats_failed");
            return -1;
        }
    }
    if (result->valid_count < CC_MIN_VALID_ROIS) {
        (void)snprintf(result->reason, sizeof(result->reason), "%s",
                       "too_few_rois");
        return 1;
    }
    result->success = 1;
    result->reason[0] = '\0';
    return 0;
}
