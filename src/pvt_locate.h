#ifndef ROI_PVT_LOCATE_H
#define ROI_PVT_LOCATE_H

#include <stdint.h>

typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int class_id;
} pvt_box_t;

typedef struct {
    int used;
    float cx;
    float cy;
    float r_eq;
    float contrast_avg;
    float contrast_min;
    float overshoot_avg;
    float overshoot_max;
    float undershoot_avg;
    float undershoot_max;
    /* Raw nearest-pixel samples, center outward: left, right, up, down. */
    float profiles[4][64];
    int profile_len[4];
    const char *reason;
    int lines_valid;     /* 1 when the knife-edge seams were fitted */
    float line_v_slope;  /* L_V: x = line_v_slope * y + line_v_offset */
    float line_v_offset;
    float line_h_slope;  /* L_H: y = line_h_slope * x + line_h_offset */
    float line_h_offset;
} pvt_target_t;

/* Return 1 when located and measured; return 0 with a static reason otherwise. */
int Pvt_LocateAndMeasure(const uint8_t *bgra, int width, int height, int stride,
                         const pvt_box_t *box, pvt_target_t *out);
/* Return 1 for complete flat-ended profiles, otherwise 0 and zero the outputs. */
int Pvt_AnalyzeProfile(const double *profile, int len, double *contrast,
                       double *overshoot_pct, double *undershoot_pct);

/* Locate the sub-pixel 50% midpoint crossing in a 1-D profile.
 * Returns 1 on success and writes the 0-based sub-pixel index to *edge_pos
 * and the platform step amplitude to *out_step; returns 0 otherwise. */
int Pvt_FindEdgeSubpixel(const double *profile, int len,
                         double *edge_pos, double *out_step);

/* Fit orthogonal lines L_V (x = a_v * y + b_v) and L_H (y = a_h * x + b_h)
 * from horizontal-cut crossings (hx, hy, hw, h_count) and vertical-cut
 * crossings (vx, vy, vw, v_count) with outlier rejection, and solve their
 * intersection (*out_cx, *out_cy). Returns 1 on success, 0 on failure. */
int Pvt_FitLineIntersection(const double *hx, const double *hy,
                            const double *hw, int h_count,
                            const double *vx, const double *vy,
                            const double *vw, int v_count,
                            double *out_cx, double *out_cy);

#endif
