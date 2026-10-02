#include "yolo_post.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int yolo_valid_image_size(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (width > INT_MAX / 4) {
        return 0;
    }
    return 1;
}

int yolo_letterbox(const uint8_t *bgra, int width, int height, int stride,
                   int bottom_up, uint8_t *rgb, yolo_letterbox_t *params)
{
    const size_t canvas_pixels = (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE;
    double scale;
    double x_ratio;
    double y_ratio;
    int resized_width;
    int resized_height;
    int pad_x;
    int pad_y;
    int y;

    if (bgra == NULL || rgb == NULL || params == NULL ||
        !yolo_valid_image_size(width, height) || stride < width * 4 ||
        stride <= 0 || (size_t)height > SIZE_MAX / (size_t)stride) {
        return -1;
    }

    scale = (double)YOLO_INPUT_SIZE / (double)width;
    if ((double)YOLO_INPUT_SIZE / (double)height < scale) {
        scale = (double)YOLO_INPUT_SIZE / (double)height;
    }
    resized_width = (int)((double)width * scale + 0.5);
    resized_height = (int)((double)height * scale + 0.5);
    if (resized_width < 1) {
        resized_width = 1;
    }
    if (resized_height < 1) {
        resized_height = 1;
    }
    if (resized_width > YOLO_INPUT_SIZE) {
        resized_width = YOLO_INPUT_SIZE;
    }
    if (resized_height > YOLO_INPUT_SIZE) {
        resized_height = YOLO_INPUT_SIZE;
    }
    pad_x = (YOLO_INPUT_SIZE - resized_width) / 2;
    pad_y = (YOLO_INPUT_SIZE - resized_height) / 2;

    memset(rgb, 114, canvas_pixels * 3U);
    x_ratio = (double)width / (double)resized_width;
    y_ratio = (double)height / (double)resized_height;

    for (y = 0; y < resized_height; ++y) {
        const double source_y = ((double)y + 0.5) * y_ratio - 0.5;
        const double clamped_y = source_y < 0.0 ? 0.0 :
                                 (source_y > (double)(height - 1) ?
                                      (double)(height - 1) : source_y);
        const int y0 = (int)clamped_y;
        const int y1 = y0 + 1 < height ? y0 + 1 : y0;
        const double fy = clamped_y - (double)y0;
        const int row0 = bottom_up ? height - 1 - y0 : y0;
        const int row1 = bottom_up ? height - 1 - y1 : y1;
        const uint8_t *top = bgra + (size_t)row0 * (size_t)stride;
        const uint8_t *bottom = bgra + (size_t)row1 * (size_t)stride;
        int x;

        for (x = 0; x < resized_width; ++x) {
            const double source_x = ((double)x + 0.5) * x_ratio - 0.5;
            const double clamped_x = source_x < 0.0 ? 0.0 :
                                     (source_x > (double)(width - 1) ?
                                          (double)(width - 1) : source_x);
            const int x0 = (int)clamped_x;
            const int x1 = x0 + 1 < width ? x0 + 1 : x0;
            const double fx = clamped_x - (double)x0;
            const size_t dst_index =
                ((size_t)(y + pad_y) * YOLO_INPUT_SIZE + (size_t)(x + pad_x)) *
                3U;
            int channel;

            for (channel = 0; channel < 3; ++channel) {
                const int bgra_channel = 2 - channel;
                const double top_value =
                    (double)top[(size_t)x0 * 4U + (size_t)bgra_channel] *
                        (1.0 - fx) +
                    (double)top[(size_t)x1 * 4U + (size_t)bgra_channel] * fx;
                const double bottom_value =
                    (double)bottom[(size_t)x0 * 4U + (size_t)bgra_channel] *
                        (1.0 - fx) +
                    (double)bottom[(size_t)x1 * 4U + (size_t)bgra_channel] * fx;
                const double value = top_value * (1.0 - fy) + bottom_value * fy;

                rgb[dst_index + (size_t)channel] = (uint8_t)(value + 0.5);
            }
        }
    }

    params->image_width = width;
    params->image_height = height;
    params->resized_width = resized_width;
    params->resized_height = resized_height;
    params->pad_x = pad_x;
    params->pad_y = pad_y;
    params->scale = scale;
    return 0;
}

int yolo_normalize_nchw(const uint8_t *rgb, float *nchw)
{
    const size_t plane = (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE;
    size_t pixel;

    if (rgb == NULL || nchw == NULL) {
        return -1;
    }
    for (pixel = 0; pixel < plane; ++pixel) {
        nchw[pixel] = (float)rgb[pixel * 3U] / 255.0f;
        nchw[plane + pixel] = (float)rgb[pixel * 3U + 1U] / 255.0f;
        nchw[plane * 2U + pixel] = (float)rgb[pixel * 3U + 2U] / 255.0f;
    }
    return 0;
}

float yolo_iou(const yolo_detection_t *left, const yolo_detection_t *right)
{
    float intersection_width;
    float intersection_height;
    float intersection;
    float left_area;
    float right_area;
    float union_area;

    if (left == NULL || right == NULL) {
        return 0.0f;
    }
    intersection_width = fminf(left->x2, right->x2) -
                         fmaxf(left->x1, right->x1);
    intersection_height = fminf(left->y2, right->y2) -
                          fmaxf(left->y1, right->y1);
    if (intersection_width <= 0.0f || intersection_height <= 0.0f) {
        return 0.0f;
    }
    intersection = intersection_width * intersection_height;
    left_area = fmaxf(0.0f, left->x2 - left->x1) *
                fmaxf(0.0f, left->y2 - left->y1);
    right_area = fmaxf(0.0f, right->x2 - right->x1) *
                 fmaxf(0.0f, right->y2 - right->y1);
    union_area = left_area + right_area - intersection;
    if (union_area <= 0.0f) {
        return 0.0f;
    }
    return intersection / union_area;
}

static int yolo_detection_compare(const void *left_pointer,
                                  const void *right_pointer)
{
    const yolo_detection_t *left = (const yolo_detection_t *)left_pointer;
    const yolo_detection_t *right = (const yolo_detection_t *)right_pointer;

    if (left->score > right->score) {
        return -1;
    }
    if (left->score < right->score) {
        return 1;
    }
    return 0;
}

int yolo_nms(yolo_detection_t *detections, size_t count, float iou_threshold,
             size_t max_det, size_t *kept_count)
{
    uint8_t *suppressed;
    size_t kept = 0;
    size_t i;

    if (kept_count == NULL || (count != 0 && detections == NULL) ||
        !isfinite(iou_threshold) || iou_threshold < 0.0f ||
        iou_threshold > 1.0f) {
        return -1;
    }
    *kept_count = 0;
    if (count == 0 || max_det == 0) {
        return 0;
    }
    suppressed = (uint8_t *)calloc(count, sizeof(*suppressed));
    if (suppressed == NULL) {
        return -1;
    }
    qsort(detections, count, sizeof(*detections), yolo_detection_compare);

    for (i = 0; i < count && kept < max_det; ++i) {
        size_t j;

        if (suppressed[i] != 0) {
            continue;
        }
        if (kept != i) {
            detections[kept] = detections[i];
        }
        for (j = i + 1; j < count; ++j) {
            if (suppressed[j] == 0 &&
                detections[kept].class_id == detections[j].class_id &&
                yolo_iou(&detections[kept], &detections[j]) > iou_threshold) {
                suppressed[j] = 1;
            }
        }
        ++kept;
    }

    free(suppressed);
    *kept_count = kept;
    return 0;
}

int yolo_decode_v8(const float *output, const yolo_letterbox_t *params,
                   float confidence_threshold, float iou_threshold,
                   size_t max_det, yolo_detection_t *detections,
                   size_t capacity, size_t *detection_count)
{
    yolo_detection_t *candidates;
    size_t candidate_count = 0;
    size_t kept_count = 0;
    size_t i;

    if (output == NULL || params == NULL || detection_count == NULL ||
        (capacity != 0 && detections == NULL) ||
        !isfinite(confidence_threshold) || confidence_threshold < 0.0f ||
        confidence_threshold > 1.0f || !isfinite(iou_threshold) ||
        iou_threshold < 0.0f || iou_threshold > 1.0f ||
        !isfinite(params->scale) || params->scale <= 0.0 ||
        params->image_width <= 0 || params->image_height <= 0) {
        return -1;
    }
    *detection_count = 0;
    candidates = (yolo_detection_t *)malloc(
        (size_t)YOLO_OUTPUT_CANDIDATES * sizeof(*candidates));
    if (candidates == NULL) {
        return -1;
    }

    for (i = 0; i < YOLO_OUTPUT_CANDIDATES; ++i) {
        const float score = output[5U * YOLO_OUTPUT_CANDIDATES + i];
        const float center_x = output[i];
        const float center_y = output[YOLO_OUTPUT_CANDIDATES + i];
        const float width = output[2U * YOLO_OUTPUT_CANDIDATES + i];
        const float height = output[3U * YOLO_OUTPUT_CANDIDATES + i];

        /* Only class 1 (color-chart) is kept; class 0 (122) is discarded.
           The score stored is always the class-1 score, not the max. */
        if (!isfinite(score) ||
            score < confidence_threshold || !isfinite(center_x) ||
            !isfinite(center_y) || !isfinite(width) || !isfinite(height) ||
            width <= 0.0f || height <= 0.0f) {
            continue;
        }
        candidates[candidate_count].x1 = center_x - width * 0.5f;
        candidates[candidate_count].y1 = center_y - height * 0.5f;
        candidates[candidate_count].x2 = center_x + width * 0.5f;
        candidates[candidate_count].y2 = center_y + height * 0.5f;
        candidates[candidate_count].score = score;
        candidates[candidate_count].class_id = 1;
        ++candidate_count;
    }

    if (yolo_nms(candidates, candidate_count, iou_threshold, max_det,
                 &kept_count) != 0) {
        free(candidates);
        return -1;
    }
    for (i = 0; i < kept_count; ++i) {
        yolo_detection_t detection = candidates[i];

        detection.x1 = (float)(((double)detection.x1 - params->pad_x) /
                               params->scale);
        detection.y1 = (float)(((double)detection.y1 - params->pad_y) /
                               params->scale);
        detection.x2 = (float)(((double)detection.x2 - params->pad_x) /
                               params->scale);
        detection.y2 = (float)(((double)detection.y2 - params->pad_y) /
                               params->scale);
        detection.x1 = fmaxf(0.0f, fminf((float)params->image_width,
                                        detection.x1));
        detection.y1 = fmaxf(0.0f, fminf((float)params->image_height,
                                        detection.y1));
        detection.x2 = fmaxf(0.0f, fminf((float)params->image_width,
                                        detection.x2));
        detection.y2 = fmaxf(0.0f, fminf((float)params->image_height,
                                        detection.y2));
        if (detection.x2 - detection.x1 < 1.0f ||
            detection.y2 - detection.y1 < 1.0f) {
            continue;
        }
        if (*detection_count >= capacity) {
            free(candidates);
            return -1;
        }
        detections[*detection_count] = detection;
        ++*detection_count;
    }

    free(candidates);
    return 0;
}
