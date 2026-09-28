#include "fft.h"

#include <math.h>
#include <stddef.h>

static BOOL is_power_of_two(int size)
{
    return size > 0 && (size & (size - 1)) == 0;
}

static void fft_1d(complex_t *values, int size)
{
    int i, j, length;
    for (i = 1, j = 0; i < size; i++) {
        int bit = size >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            complex_t swap = values[i];
            values[i] = values[j];
            values[j] = swap;
        }
    }
    for (length = 2; length <= size; length <<= 1) {
        int half = length >> 1;
        double angle = -6.28318530717958647692 / length;
        double step_r = cos(angle), step_i = sin(angle);
        int start;
        for (start = 0; start < size; start += length) {
            double wr = 1.0, wi = 0.0;
            for (j = 0; j < half; j++) {
                complex_t even = values[start + j];
                complex_t odd = values[start + j + half];
                double tr = wr * odd.r - wi * odd.i;
                double ti = wr * odd.i + wi * odd.r;
                values[start + j].r = even.r + tr;
                values[start + j].i = even.i + ti;
                values[start + j + half].r = even.r - tr;
                values[start + j + half].i = even.i - ti;
                {
                    double next_wr = wr * step_r - wi * step_i;
                    wi = wr * step_i + wi * step_r;
                    wr = next_wr;
                }
            }
        }
        if (length == size)
            break;
    }
}

BOOL FFT_Compute2D_Radix2(const double *gray_in, int size, BOOL apply_hann,
                          complex_t *out_complex)
{
    complex_t column[FFT_MAX_SIZE];
    int x, y;
    if (!gray_in || !out_complex || size < 2 ||
        size > FFT_MAX_SIZE || !is_power_of_two(size))
        return FALSE;
    for (y = 0; y < size; y++) {
        double wy = 1.0;
        if (apply_hann)
            wy = 0.5 - 0.5 * cos(6.28318530717958647692 * y /
                                  (double)(size - 1));
        for (x = 0; x < size; x++) {
            double wx = 1.0;
            size_t index = (size_t)y * (size_t)size + (size_t)x;
            if (apply_hann)
                wx = 0.5 - 0.5 * cos(6.28318530717958647692 * x /
                                      (double)(size - 1));
            out_complex[index].r = gray_in[index] * wx * wy;
            out_complex[index].i = 0.0;
        }
        fft_1d(out_complex + (size_t)y * (size_t)size, size);
    }
    for (x = 0; x < size; x++) {
        for (y = 0; y < size; y++)
            column[y] = out_complex[(size_t)y * (size_t)size + (size_t)x];
        fft_1d(column, size);
        for (y = 0; y < size; y++)
            out_complex[(size_t)y * (size_t)size + (size_t)x] = column[y];
    }
    return TRUE;
}

void FFT_CalculateEnergyBands(const complex_t *spec, int size,
                              double *out_low, double *out_mid,
                              double *out_high, double *out_high_ratio)
{
    double low = 0.0, mid = 0.0, high = 0.0;
    int x, y;
    if (!spec || size <= 0) {
        if (out_low) *out_low = 0.0;
        if (out_mid) *out_mid = 0.0;
        if (out_high) *out_high = 0.0;
        if (out_high_ratio) *out_high_ratio = 0.0;
        return;
    }
    for (y = 0; y < size; y++) {
        int fy = y <= size / 2 ? y : y - size;
        for (x = 0; x < size; x++) {
            int fx = x <= size / 2 ? x : x - size;
            double radius = sqrt((double)fx * fx + (double)fy * fy) /
                            (double)size;
            const complex_t *value =
                &spec[(size_t)y * (size_t)size + (size_t)x];
            double power = value->r * value->r + value->i * value->i;
            if (radius < 0.1)
                low += power;
            else if (radius < 0.25)
                mid += power;
            else if (radius <= 0.5)
                high += power;
        }
    }
    if (out_low) *out_low = low;
    if (out_mid) *out_mid = mid;
    if (out_high) *out_high = high;
    if (out_high_ratio)
        *out_high_ratio = (low + mid + high) > 0.0 ?
                          high * 100.0 / (low + mid + high) : 0.0;
}
