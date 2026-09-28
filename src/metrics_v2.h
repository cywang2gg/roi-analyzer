#ifndef ROI_METRICS_V2_H
#define ROI_METRICS_V2_H

#include "metrics.h"
#include "masks.h"

void MetricsV2_Init(metrics_item_result_t *item);
BOOL MetricsV2_Compute(const image_t *img, const metrics_masks_t *masks,
                       volatile BOOL *cancel_requested,
                       metrics_item_result_t *item);

#endif
