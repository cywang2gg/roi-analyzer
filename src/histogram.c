#include "histogram.h"

#include <math.h>
#include <string.h>

void Hist_Reset(histogram_t *h)
{
    if (h)
        memset(h, 0, sizeof(*h));
}

static void finish_channel(histogram_t *h, int ch)
{
    double sum = 0.0, sum2 = 0.0, variance;
    unsigned int cumulative = 0;
    unsigned int threshold = h->count / 2U + h->count % 2U;
    BOOL found_median = FALSE;
    int level;

    for (level = 0; level < 256; level++) {
        unsigned int bin = h->bin[ch][level];
        sum += (double)level * bin;
        sum2 += (double)level * (double)level * bin;
        if (bin > h->max_bin[ch])
            h->max_bin[ch] = bin;
        cumulative += bin;
        if (!found_median && cumulative >= threshold) {
            h->median[ch] = level;
            found_median = TRUE;
        }
    }
    if (h->count == 0)
        return;
    h->mean[ch] = sum / h->count;
    variance = sum2 / h->count - h->mean[ch] * h->mean[ch];
    h->std[ch] = sqrt(variance > 0.0 ? variance : 0.0);
}

void Hist_Compute(const image_t *img, const RECT *rc, unsigned int img_gen,
                  histogram_t *out)
{
    RECT area;
    int x, y, ch;

    if (!out)
        return;
    Hist_Reset(out);
    if (!img || !img->valid || !img->px || img->w <= 0 || img->h <= 0 ||
        img->pitch < img->w * 4)
        return;
    area.left = rc ? rc->left : 0;
    area.top = rc ? rc->top : 0;
    area.right = rc ? rc->right : img->w - 1;
    area.bottom = rc ? rc->bottom : img->h - 1;
    if (area.left > area.right) {
        int t = area.left;
        area.left = area.right;
        area.right = t;
    }
    if (area.top > area.bottom) {
        int t = area.top;
        area.top = area.bottom;
        area.bottom = t;
    }
    if (area.left < 0)
        area.left = 0;
    if (area.top < 0)
        area.top = 0;
    if (area.right >= img->w)
        area.right = img->w - 1;
    if (area.bottom >= img->h)
        area.bottom = img->h - 1;
    if (area.right < area.left || area.bottom < area.top)
        return;

    for (y = area.top; y <= area.bottom; y++) {
        const unsigned char *row =
            img->px + (size_t)y * (size_t)img->pitch;
        for (x = area.left; x <= area.right; x++) {
            unsigned int b = row[x * 4];
            unsigned int g = row[x * 4 + 1];
            unsigned int r = row[x * 4 + 2];
            unsigned int yy = (299U * r + 587U * g + 114U * b + 500U) / 1000U;
            out->bin[HIST_R][r]++;
            out->bin[HIST_G][g]++;
            out->bin[HIST_B][b]++;
            out->bin[HIST_Y][yy]++;
            out->count++;
        }
    }
    out->src = area;
    out->whole = rc == NULL;
    out->img_gen = img_gen;
    out->valid = TRUE;
    for (ch = 0; ch < HIST_NCH; ch++)
        finish_channel(out, ch);
}

void Hist_RangeStats(const histogram_t *h, int ch, int lo, int hi,
                     hist_range_t *out)
{
    double sum = 0.0, sum2 = 0.0, variance;
    unsigned int cumulative = 0;
    int level;

    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!h || !h->valid || ch < 0 || ch >= HIST_NCH)
        return;
    if (lo < 0)
        lo = 0;
    if (lo > 255)
        lo = 255;
    if (hi < 0)
        hi = 0;
    if (hi > 255)
        hi = 255;
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    for (level = lo; level <= hi; level++) {
        unsigned int bin = h->bin[ch][level];
        out->count += bin;
        sum += (double)level * bin;
        sum2 += (double)level * (double)level * bin;
    }
    for (level = 0; level <= hi; level++)
        cumulative += h->bin[ch][level];
    if (out->count > 0) {
        out->mean = sum / out->count;
        variance = sum2 / out->count - out->mean * out->mean;
        out->std = sqrt(variance > 0.0 ? variance : 0.0);
    }
    if (h->count > 0)
        out->percentile = (double)cumulative * 100.0 / h->count;
}
