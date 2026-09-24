#ifndef ROI_ANALYZE_H
#define ROI_ANALYZE_H

#include <windows.h>

struct image_s;
typedef struct image_s image_t;

// One ROI analysis result (rect in image coords, inclusive).
typedef struct {
    int x0, y0, x1, y1;
    int count;
    double r_mean, r_std, g_mean, g_std, b_mean, b_std; // blue = b_*
    double y_mean, y_std;                               // BT.601 luma
    double lab_l, lab_a, lab_b;                         // from mean RGB, D65
} roi_result_t;

// Single pass: sum + sumsq -> mean, population std. rect clamped + ordered.
void AnalyzeROI(const image_t *img, RECT rc, roi_result_t *out);

// rgb_to_lab ported as-is from c-vlcplayer src/analysis.c (D65, sRGB).
void rgb_to_lab_f(float r, float g, float b, float *l, float *a, float *bl);

#endif
