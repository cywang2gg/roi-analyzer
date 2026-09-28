#ifndef ROI_METRICS_H
#define ROI_METRICS_H

#include <windows.h>
#include <stddef.h>

#include "fft.h"
#include "image.h"
#include "masks.h"

#define METRICS_V2_COUNT 23
#define METRICS_REASON_LENGTH 48

typedef enum {
    METRIC_S1_EDGE_GAIN = 0,
    METRIC_S2_EDGE_WIDTH,
    METRIC_S3_OVERSHOOT,
    METRIC_S3_UNDERSHOOT,
    METRIC_S4_LOW_GAIN,
    METRIC_S4_MID_GAIN,
    METRIC_S4_HIGH_GAIN,
    METRIC_S4_TIER_RATIO,
    METRIC_S5_LOW_SURVIVAL,
    METRIC_S5_MID_SURVIVAL,
    METRIC_S5_HIGH_SURVIVAL,
    METRIC_N1_NOISE_SIGMA,
    METRIC_N1_SNR_DB,
    METRIC_N2_RESIDUAL_FWHM,
    METRIC_N3_SHADOW_SIGMA,
    METRIC_C1_CHROMA_SIGMA,
    METRIC_C2_CHROMA_SIGMA,
    METRIC_C2_BLOTCH_FWHM,
    METRIC_C3_GRAY_CAST,
    METRIC_K1_CHROMA_MEAN,
    METRIC_K1_CHROMA_P95,
    METRIC_K1_CHROMA_DELTA_PERCENT,
    METRIC_K2_HUE_DELTA
} metrics_v2_id_t;

#define METRICS_ZONE_SHADOW 0
#define METRICS_ZONE_LOW    1
#define METRICS_ZONE_MID    2
#define METRICS_ZONE_HIGH   3
#define METRICS_ZONE_COUNT  4

typedef struct {
    double laplacian_var;
    double sobel_mean;
    double tenengrad;
    double brenner;
    double edge_density;
    double quality_score;
    double cumulative_contrast;
    double noise_estimate;
    double snr_db;
    double multi_dir_contrast;
    double zone_contrast[METRICS_ZONE_COUNT];
    double zone_mean_range;
    double sat_mean;
    double sat_std;
    double sat_low_ratio;
    double sat_high_ratio;
} metrics_stage1_t;

typedef struct {
    double fft_low_energy;
    double fft_mid_energy;
    double fft_high_energy;
    double fft_high_ratio;
    double gray_cast_delta_e;
    double gray_cast_lab_a;
    double gray_cast_lab_b;
    char gray_cast_text[16];
    double shadow_lab_a;
    double shadow_lab_b;
    char shadow_defect[16];
    char *edge_png_base64;
    size_t edge_png_base64_len;
    double mean_lab_l;
    double mean_lab_a;
    double mean_lab_b;
    double mean_chroma;
    double chroma_p95;
    double chroma_delta_percent;
    double hue_delta;
    double noise_residual_fwhm;
    double chroma_blotch_fwhm;
    double gamma_l[5];
    char gamma_reason[5][METRICS_REASON_LENGTH];
    char *v2_png_base64[3];
    size_t v2_png_base64_len[3];
} metrics_stage2_t;

typedef struct {
    char image_name[MAX_PATH];
    RECT source_rect;
    int image_w;
    int image_h;
    BOOL has_stage2;
    metrics_stage1_t s1;
    metrics_stage2_t s2;
    double v2[METRICS_V2_COUNT];
    char v2_reason[METRICS_V2_COUNT][METRICS_REASON_LENGTH];
    double rank_score;
    double rank_category[4];
    int rank_order;
    BOOL rank_tied;
    double v2_edge_count[3];
} metrics_item_result_t;

typedef struct {
    double *row_buf_y[3];
    int buf_w;
    complex_t *fft_buf;
} metrics_workspace_t;

BOOL Metrics_InitWorkspace(metrics_workspace_t *ws, int max_w);
void Metrics_FreeWorkspace(metrics_workspace_t *ws);
BOOL Metrics_ComputeSpatial(const image_t *img, RECT rc,
                            metrics_workspace_t *ws,
                            metrics_stage1_t *out_s1);
BOOL Metrics_ComputeFrequencyAndColor(const image_t *img, RECT rc,
                                      metrics_workspace_t *ws,
                                      metrics_stage2_t *out_s2);
BOOL Metrics_AnalyzeROI(const image_t *img, RECT rc, BOOL enable_stage2,
                        metrics_item_result_t *out);
BOOL Metrics_AnalyzeROI_Cancelable(const image_t *img, RECT rc,
                                   BOOL enable_stage2,
                                   volatile BOOL *cancel_requested,
                                   metrics_item_result_t *out);
BOOL Metrics_AnalyzeROI_WithReferenceMask(
    const image_t *img, RECT rc, BOOL enable_stage2,
    const metrics_masks_t *reference_masks, metrics_item_result_t *out);
BOOL Metrics_AnalyzeROI_CancelableWithReferenceMask(
    const image_t *img, RECT rc, BOOL enable_stage2,
    volatile BOOL *cancel_requested,
    const metrics_masks_t *reference_masks, metrics_item_result_t *out);
void Metrics_FreeItemResult(metrics_item_result_t *item);
/* reference_index == -1 uses the result-set average as the baseline. */
void Metrics_ApplyReference(metrics_item_result_t *items, int count,
                            int reference_index);

#endif
