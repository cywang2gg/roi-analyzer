#ifndef ROI_METRIC_VIEWS_H
#define ROI_METRIC_VIEWS_H

#include "metrics.h"
#include "masks.h"

BOOL MetricsViews_Build(const image_t *img, const metrics_masks_t *masks,
                        metrics_stage2_t *stage2);
void MetricsViews_Free(metrics_stage2_t *stage2);

#endif
