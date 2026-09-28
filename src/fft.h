#ifndef ROI_FFT_H
#define ROI_FFT_H

#include <windows.h>

typedef struct {
    double r;
    double i;
} complex_t;

#define FFT_MAX_SIZE 512

BOOL FFT_Compute2D_Radix2(const double *gray_in, int size, BOOL apply_hann,
                          complex_t *out_complex);
void FFT_CalculateEnergyBands(const complex_t *spec, int size,
                              double *out_low, double *out_mid,
                              double *out_high, double *out_high_ratio);

#endif
