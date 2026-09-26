#ifndef ROI_HISTOGRAM_H
#define ROI_HISTOGRAM_H

#include <windows.h>

#include "image.h"

enum { HIST_R = 0, HIST_G = 1, HIST_B = 2, HIST_Y = 3, HIST_NCH = 4 };

typedef struct {
    unsigned int bin[HIST_NCH][256];
    unsigned int max_bin[HIST_NCH];
    double mean[HIST_NCH];
    double std[HIST_NCH];
    int median[HIST_NCH];
    unsigned int count;
    RECT src;
    BOOL whole;
    unsigned int img_gen;
    BOOL valid;
} histogram_t;

typedef struct {
    unsigned int count;
    double mean;
    double std;
    double percentile;
} hist_range_t;

void Hist_Compute(const image_t *img, const RECT *rc, unsigned int img_gen,
                  histogram_t *out);
void Hist_RangeStats(const histogram_t *h, int ch, int lo, int hi,
                     hist_range_t *out);
void Hist_Reset(histogram_t *h);

#endif
