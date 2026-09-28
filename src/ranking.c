#include "ranking.h"

#include <math.h>
#include <string.h>

#define CMP_MAX_CELLS 4
#define CATEGORY_COUNT 4
#define SPEC_COUNT 13

static const metrics_rank_spec_t s_specs[SPEC_COUNT] = {
    { METRIC_S1_EDGE_GAIN, METRICS_RANK_HIGHER, 0, 0, 20, 0, 1.0 },
    { METRIC_S2_EDGE_WIDTH, METRICS_RANK_LOWER, 0, 0, 4, 0, 0.8 },
    { METRIC_S3_OVERSHOOT, METRICS_RANK_LOWER, 0, 0, 20, 0, 0.7 },
    { METRIC_N1_NOISE_SIGMA, METRICS_RANK_LOWER, 1, 0, 15, 0, 1.0 },
    { METRIC_N1_SNR_DB, METRICS_RANK_HIGHER, 1, 0, 40, 0, 0.7 },
    { METRIC_N2_RESIDUAL_FWHM, METRICS_RANK_TARGET, 1, 0, 16, 2, 0.5 },
    { METRIC_N3_SHADOW_SIGMA, METRICS_RANK_LOWER, 1, 0, 20, 0, 0.8 },
    { METRIC_C1_CHROMA_SIGMA, METRICS_RANK_LOWER, 2, 0, 15, 0, 0.8 },
    { METRIC_C2_CHROMA_SIGMA, METRICS_RANK_LOWER, 2, 0, 12, 0, 0.7 },
    { METRIC_C3_GRAY_CAST, METRICS_RANK_LOWER, 2, 0, 10, 0, 0.7 },
    { METRIC_K1_CHROMA_DELTA_PERCENT, METRICS_RANK_TARGET, 3, -100, 100, 0, 0.6 },
    { METRIC_S4_TIER_RATIO, METRICS_RANK_TARGET, 0, 0, 4, 1, 0.5 },
    { METRIC_S5_HIGH_SURVIVAL, METRICS_RANK_HIGHER, 0, 0, 120, 100, 0.6 }
};

static const double s_profile_weights[METRICS_PROFILE_COUNT][CATEGORY_COUNT] = {
    { 0.30, 0.25, 0.25, 0.20 },
    { 0.55, 0.20, 0.10, 0.15 },
    { 0.20, 0.50, 0.15, 0.15 },
    { 0.20, 0.15, 0.45, 0.20 }
};

double MetricsRank_Normalize(double value, double minimum, double maximum,
                             const metrics_rank_spec_t *spec,
                             BOOL absolute_ranges)
{
    double low, high, score;
    if (!spec || !isfinite(value))
        return NAN;
    low = absolute_ranges ? spec->low : minimum;
    high = absolute_ranges ? spec->high : maximum;
    if (spec->direction == METRICS_RANK_TARGET) {
        double span;
        if (absolute_ranges) {
            span = fmax(fabs(spec->target - low), fabs(high - spec->target));
        } else {
            span = high - low;
        }
        if (span <= 1e-12)
            return 100.0;
        score = 100.0 * (1.0 - fabs(value - spec->target) / span);
    } else if (spec->direction == METRICS_RANK_RANGE) {
        if (value >= low && value <= high)
            return 100.0;
        if (value < low)
            score = high > low ? 100.0 * (1.0 - (low - value) /
                                                 (high - low)) : 0.0;
        else
            score = high > low ? 100.0 * (1.0 - (value - high) /
                                                 (high - low)) : 0.0;
    } else {
        if (high - low <= 1e-12)
            return 100.0;
        score = 100.0 * (value - low) / (high - low);
        if (spec->direction == METRICS_RANK_LOWER)
            score = 100.0 - score;
    }
    if (score < 0.0) return 0.0;
    if (score > 100.0) return 100.0;
    return score;
}

BOOL MetricsRank_OutOfRange(int metric_id, double value)
{
    int i;
    if (!isfinite(value))
        return FALSE;
    for (i = 0; i < SPEC_COUNT; i++)
        if (s_specs[i].metric_id == metric_id)
            return value < s_specs[i].low || value > s_specs[i].high;
    return FALSE;
}

void MetricsRank_Compute(metrics_item_result_t *items, int count,
                         metrics_profile_t profile, BOOL absolute_ranges)
{
    double category_score[CMP_MAX_CELLS][CATEGORY_COUNT] = { { 0.0 } };
    double category_weight[CMP_MAX_CELLS][CATEGORY_COUNT] = { { 0.0 } };
    double totals[CMP_MAX_CELLS] = { 0.0 };
    int i, j, c;
    if (!items || count <= 0 || count > CMP_MAX_CELLS)
        return;
    if (profile < 0 || profile >= METRICS_PROFILE_COUNT)
        profile = METRICS_PROFILE_BALANCED;
    for (j = 0; j < SPEC_COUNT; j++) {
        double minimum = INFINITY, maximum = -INFINITY;
        const metrics_rank_spec_t *spec = &s_specs[j];
        for (i = 0; i < count; i++) {
            double value = items[i].v2[spec->metric_id];
            if (isfinite(value)) {
                if (value < minimum) minimum = value;
                if (value > maximum) maximum = value;
            }
        }
        if (!isfinite(minimum))
            continue;
        for (i = 0; i < count; i++) {
            double value = items[i].v2[spec->metric_id];
            double score = MetricsRank_Normalize(value, minimum, maximum,
                                                  spec, absolute_ranges);
            if (isfinite(score)) {
                c = spec->category;
                category_score[i][c] += score * spec->weight;
                category_weight[i][c] += spec->weight;
            }
        }
    }
    for (i = 0; i < count; i++) {
        double total_weight = 0.0;
        for (c = 0; c < CATEGORY_COUNT; c++) {
            items[i].rank_category[c] = category_weight[i][c] > 0.0 ?
                category_score[i][c] / category_weight[i][c] : NAN;
            if (isfinite(items[i].rank_category[c])) {
                totals[i] += items[i].rank_category[c] *
                             s_profile_weights[profile][c];
                total_weight += s_profile_weights[profile][c];
            }
        }
        items[i].rank_score = total_weight > 0.0 ?
                              totals[i] / total_weight : NAN;
        items[i].rank_order = isfinite(items[i].rank_score) ? 1 : 0;
        items[i].rank_tied = FALSE;
    }
    for (i = 0; i < count; i++) {
        int other;
        for (other = 0; other < count; other++) {
            if (!isfinite(items[i].rank_score) ||
                !isfinite(items[other].rank_score))
                continue;
            if (totals[other] > totals[i] + 2.0)
                items[i].rank_order++;
            else if (other != i && fabs(totals[other] - totals[i]) <= 2.0)
                items[i].rank_tied = TRUE;
        }
    }
    (void)memset(category_score, 0, sizeof(category_score));
}
