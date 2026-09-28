#ifndef ROI_RANKING_H
#define ROI_RANKING_H

#include "metrics.h"

typedef enum {
    METRICS_RANK_HIGHER = 0,
    METRICS_RANK_LOWER,
    METRICS_RANK_TARGET,
    METRICS_RANK_RANGE
} metrics_rank_direction_t;

typedef enum {
    METRICS_PROFILE_BALANCED = 0,
    METRICS_PROFILE_DETAIL,
    METRICS_PROFILE_LOW_LIGHT,
    METRICS_PROFILE_COLOR,
    METRICS_PROFILE_COUNT
} metrics_profile_t;

typedef struct {
    int metric_id;
    metrics_rank_direction_t direction;
    int category;
    double low;
    double high;
    double target;
    double weight;
} metrics_rank_spec_t;

#ifndef METRICS_RANK_ABSOLUTE_DEFAULT
#define METRICS_RANK_ABSOLUTE_DEFAULT 1
#endif

void MetricsRank_Compute(metrics_item_result_t *items, int count,
                         metrics_profile_t profile, BOOL absolute_ranges);
double MetricsRank_Normalize(double value, double minimum, double maximum,
                             const metrics_rank_spec_t *spec,
                             BOOL absolute_ranges);
BOOL MetricsRank_OutOfRange(int metric_id, double value);

#endif
