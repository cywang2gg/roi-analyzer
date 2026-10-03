#ifndef ROI_CC_LOCATE_H
#define ROI_CC_LOCATE_H

#include <stddef.h>
#include <stdint.h>

#define CC_H_LINES_LAND 5
#define CC_V_LINES_LAND 7
#define CC_H_LINES_PORT 7
#define CC_V_LINES_PORT 5
#define CC_H_LINES 5
#define CC_V_LINES 7
#define CC_MAX_H_LINES 7
#define CC_MAX_V_LINES 7
#define CC_ROI_COUNT 24
#define CC_MIN_VALID_ROIS 23
#define CC_INSET_RATIO 0.3

typedef struct {
    const uint8_t *pixels;
    int width;
    int height;
    int stride;
    int channels;
} cc_image_view_t;

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int stride;
    int channels;
} cc_image_t;

typedef struct {
    float xc;
    float yc;
    float width;
    float height;
} cc_box_t;

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
} cc_segment_t;

typedef struct {
    int id;
    int x1;
    int y1;
    int x2;
    int y2;
    int valid;
    float display_x1;
    float display_y1;
    float display_x2;
    float display_y2;
} cc_roi_t;

typedef struct {
    double mean_b;
    double mean_g;
    double mean_r;
    double y_mean;
    double y_std;
    double l_star;
    double a_star;
    double b_star;
    double chroma;
    double l_noise;
    int lab_valid;
} cc_roi_stats_t;

typedef struct {
    float h[CC_MAX_H_LINES];
    float v[CC_MAX_V_LINES];
    size_t h_count;
    size_t v_count;
    size_t h_target;
    size_t v_target;
    int swapped;
    int h_good;
    int v_good;
    const char *reason;
} cc_grid_t;

typedef struct {
    int32_t seq;
    double elapsed_ms;
    int status;
    int success;
    size_t valid_count;
    cc_grid_t grid;
    cc_roi_t rois[CC_ROI_COUNT];
    cc_roi_stats_t stats[CC_ROI_COUNT];
    char reason[64];
} cc_locate_result_t;

int cc_crop_scale(cc_image_view_t source, cc_box_t box,
                  float scale_factor, int max_dimension,
                  cc_image_t *cropped, int *origin_x, int *origin_y,
                  float *rx, float *ry);
void cc_image_free(cc_image_t *image);
int cc_merge_positions(const cc_segment_t *segments, size_t segment_count,
                       float spacing_threshold, float *horizontal,
                       size_t horizontal_capacity, size_t *horizontal_count,
                       float *vertical, size_t vertical_capacity,
                       size_t *vertical_count);
int cc_refine_axis(const float *positions, size_t position_count, int dim,
                   size_t target_count, float forced_spacing,
                   int has_forced_spacing, float *refined,
                   size_t refined_capacity, size_t *refined_count,
                   float *typical_spacing, int *is_good);
int cc_grid_refine(const float *horizontal, size_t horizontal_count,
                   const float *vertical, size_t vertical_count,
                   int width, int height, cc_grid_t *grid);
int cc_canon_id(int img_row, int img_col, int rows, int cols, int rot);
int cc_np_slice(int start, int stop, int len, int *out_start);
int cc_extract_rois(const float *horizontal, size_t horizontal_count,
                    const float *vertical, size_t vertical_count,
                    int image_width, int image_height,
                    int origin_x, int origin_y, float rx, float ry,
                    cc_roi_t rois[CC_ROI_COUNT], size_t *valid_count);
int cc_roi_stats(const cc_image_view_t image, const cc_roi_t *roi,
                 cc_roi_stats_t *stats);
int cc_locate_run(cc_image_view_t image, int origin_x, int origin_y,
                  float rx, float ry, cc_locate_result_t *result);

#endif
