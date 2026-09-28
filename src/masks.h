#ifndef ROI_MASKS_H
#define ROI_MASKS_H

#include "image.h"

#include <stddef.h>
#include <stdint.h>

#define METRICS_MASK_EDGE       0x01
#define METRICS_MASK_FLAT       0x02
#define METRICS_MASK_NEUTRAL    0x04
#define METRICS_MASK_TIER_SHIFT 3
#define METRICS_MASK_TIER_MASK  0x18

typedef struct metrics_masks {
    RECT rect;
    int width;
    int height;
    size_t count;
    uint8_t *classes;
    BOOL owns_classes;
    const struct metrics_masks *reference;
    double edge_threshold;
    double flat_threshold;
} metrics_masks_t;

BOOL MetricsMasks_Build(const image_t *img, RECT rect,
                        metrics_masks_t *masks);
BOOL MetricsMasks_BuildCancelable(const image_t *img, RECT rect,
                                 volatile BOOL *cancel_requested,
                                 metrics_masks_t *masks);
BOOL MetricsMasks_UseReference(const metrics_masks_t *reference, RECT rect,
                               metrics_masks_t *mapped);
uint8_t MetricsMasks_ClassAt(const metrics_masks_t *masks, int x, int y);
void MetricsMasks_Free(metrics_masks_t *masks);

#endif
