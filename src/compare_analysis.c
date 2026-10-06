#include "compare.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "analyze.h"

#define TEX_EDGE_T 160
#define NEU_CHROMA 10.0f
#define ANA_NEU_L_MIN 8.0f
#define ANA_NEU_L_MAX 96.0f
#define NEU_GAIN 4.0f

static BYTE s_gam[65536];
static BOOL s_gam_ready;

static BOOL alloc_like(image_t *dst, const image_t *src)
{
    size_t bytes;
    if (!dst || !src || !src->valid || !src->px ||
        src->w <= 0 || src->h <= 0 || src->w > INT_MAX / 4)
        return FALSE;
    if ((double)src->w * (double)src->h > CMP_ANA_MAX_PIXELS)
        return FALSE;
    if ((size_t)src->w > SIZE_MAX / (size_t)src->h / 4)
        return FALSE;
    bytes = (size_t)src->w * (size_t)src->h * 4;
    ZeroMemory(dst, sizeof(*dst));
    dst->px = (unsigned char *)malloc(bytes);
    if (!dst->px)
        return FALSE;
    dst->w = src->w;
    dst->h = src->h;
    dst->pitch = src->w * 4;
    lstrcpynA(dst->path, src->path, MAX_PATH);
    lstrcpynA(dst->decoder, src->decoder, sizeof(dst->decoder));
    dst->valid = TRUE;
    return TRUE;
}

static BYTE *make_luma_padded(const image_t *img, int *stride_out)
{
    BYTE *luma;
    size_t bytes;
    int stride, rows, x, y;
    if (!img || !img->valid || !img->px || img->w <= 0 || img->h <= 0 ||
        img->w > INT_MAX / 4 || img->pitch < img->w * 4 ||
        img->w > INT_MAX - 4 || img->h > INT_MAX - 4)
        return NULL;
    stride = img->w + 4;
    rows = img->h + 4;
    if ((size_t)stride > SIZE_MAX / (size_t)rows)
        return NULL;
    bytes = (size_t)stride * (size_t)rows;
    luma = (BYTE *)malloc(bytes);
    if (!luma)
        return NULL;
    for (y = -2; y < img->h + 2; y++) {
        int sy = y < 0 ? 0 : (y >= img->h ? img->h - 1 : y);
        for (x = -2; x < img->w + 2; x++) {
            int sx = x < 0 ? 0 : (x >= img->w ? img->w - 1 : x);
            const BYTE *p = img->px + (size_t)sy * (size_t)img->pitch +
                            (size_t)sx * 4;
            luma[(size_t)(y + 2) * (size_t)stride + (size_t)(x + 2)] =
                (BYTE)((77 * p[2] + 150 * p[1] + 29 * p[0] + 128) >> 8);
        }
    }
    *stride_out = stride;
    return luma;
}

static int sobel_l1(const BYTE *center, int stride)
{
    int gx, gy;
    gx = -(int)center[-stride - 1] + (int)center[-stride + 1]
         - 2 * (int)center[-1] + 2 * (int)center[1]
         - (int)center[stride - 1] + (int)center[stride + 1];
    gy = -(int)center[-stride - 1] - 2 * (int)center[-stride]
         - (int)center[-stride + 1] + (int)center[stride - 1]
         + 2 * (int)center[stride] + (int)center[stride + 1];
    return abs(gx) + abs(gy);
}

static BOOL is_neutral(float L, float A, float B)
{
    float chroma = sqrtf(A * A + B * B);
    return chroma < NEU_CHROMA && L >= ANA_NEU_L_MIN &&
           L <= ANA_NEU_L_MAX;
}

static BOOL build_sharp(const image_t *src, image_t *dst)
{
    BYTE *luma;
    int stride, x, y;
    if (!alloc_like(dst, src))
        return FALSE;
    luma = make_luma_padded(src, &stride);
    if (!luma) {
        Image_Free(dst);
        return FALSE;
    }
    for (y = 0; y < src->h; y++) {
        for (x = 0; x < src->w; x++) {
            int g = sobel_l1(luma + (size_t)(y + 2) * (size_t)stride +
                             (size_t)(x + 2), stride);
            int value = (int)floor(255.0 * sqrt((double)(g > 1020 ? 1020 : g) /
                                                1020.0) + 0.5);
            BYTE *p = dst->px + (size_t)y * (size_t)dst->pitch +
                      (size_t)x * 4;
            p[0] = p[1] = p[2] = (BYTE)value;
            p[3] = 255;
        }
    }
    free(luma);
    return TRUE;
}

static BOOL build_texture(const image_t *src, image_t *dst)
{
    BYTE *luma;
    WORD *col;
    int stride, w, h, x, y;
    if (!alloc_like(dst, src))
        return FALSE;
    w = src->w;
    h = src->h;
    if (w <= 0 || h <= 0) {
        Image_Free(dst);
        return FALSE;
    }
    luma = make_luma_padded(src, &stride);
    col = (WORD *)calloc((size_t)w, sizeof(*col));
    if (!luma || !col) {
        free(luma);
        free(col);
        Image_Free(dst);
        return FALSE;
    }
    for (x = 0; x < w; x++) {
        int k, sum = 0;
        for (k = -2; k <= 2; k++)
            sum += luma[(size_t)(k + 2) * (size_t)stride + (size_t)(x + 2)];
        col[x] = (WORD)sum;
    }
    for (y = 0; y < h; y++) {
        int sum = 0;
        for (x = -2; x <= 2; x++) {
            int sx = x < 0 ? 0 : (x >= w ? w - 1 : x);
            sum += col[sx];
        }
        for (x = 0; x < w; x++) {
            const BYTE *center = luma + (size_t)(y + 2) * (size_t)stride +
                                 (size_t)(x + 2);
            int residual = abs(25 * (int)center[0] - sum);
            int g = sobel_l1(center, stride);
            BYTE *p = dst->px + (size_t)y * (size_t)dst->pitch +
                      (size_t)x * 4;
            if (g >= TEX_EDGE_T) {
                p[0] = 70;
                p[1] = 30;
                p[2] = 0;
            } else {
                double scaled = (double)residual / 25.0;
                int value;
                if (scaled > 32.0)
                    scaled = 32.0;
                value = (int)floor(255.0 * sqrt(scaled / 32.0) + 0.5);
                p[0] = p[1] = p[2] = (BYTE)value;
            }
            p[3] = 255;
            if (x + 1 < w) {
                int leaving = x - 2;
                int entering = x + 3;
                int k;
                if (leaving < -2)
                    leaving = -2;
                if (entering >= w + 2)
                    entering = w + 1;
                for (k = -2; k <= 2; k++)
                    sum += luma[(size_t)(y + k + 2) * (size_t)stride +
                                (size_t)(entering + 2)] -
                           luma[(size_t)(y + k + 2) * (size_t)stride +
                                (size_t)(leaving + 2)];
            }
        }
        if (y + 1 < h) {
            int leaving = y - 2;
            int entering = y + 3;
            if (leaving < -2)
                leaving = -2;
            if (entering >= h + 2)
                entering = h + 1;
            for (x = 0; x < w; x++)
                col[x] = (WORD)((int)col[x] +
                    (int)luma[(size_t)(entering + 2) * (size_t)stride +
                              (size_t)(x + 2)] -
                    (int)luma[(size_t)(leaving + 2) * (size_t)stride +
                              (size_t)(x + 2)]);
        }
    }
    free(col);
    free(luma);
    return TRUE;
}

static void init_s_gam(void)
{
    int i;
    if (s_gam_ready)
        return;
    for (i = 0; i < 65536; i++) {
        double v = (double)i / 65535.0;
        double s = v <= 0.0031308 ? 12.92 * v :
                   1.055 * pow(v, 1.0 / 2.4) - 0.055;
        int value = (int)floor(s * 255.0 + 0.5);
        if (value < 0)
            value = 0;
        if (value > 255)
            value = 255;
        s_gam[i] = (BYTE)value;
    }
    s_gam_ready = TRUE;
}

static double finv(double value)
{
    if (value * value * value > 0.008856)
        return value * value * value;
    return (value - 16.0 / 116.0) / 7.787;
}

static BYTE gamma_byte(double value)
{
    int index;
    if (value < 0.0)
        value = 0.0;
    if (value > 1.0)
        value = 1.0;
    index = (int)floor(value * 65535.0 + 0.5);
    return s_gam[index];
}

static void lab_to_rgb(float l, float a, float b, BYTE *r, BYTE *g, BYTE *bl)
{
    double fy = (l + 16.0) / 116.0;
    double fx = fy + a / 500.0;
    double fz = fy - b / 200.0;
    double x = 0.95047 * finv(fx);
    double y = finv(fy);
    double z = 1.08883 * finv(fz);
    double R, G, B;
    R = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    G = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    B = 0.0557 * x - 0.2040 * y + 1.0570 * z;
    *r = gamma_byte(R);
    *g = gamma_byte(G);
    *bl = gamma_byte(B);
}

static BOOL build_neutral(const image_t *src, image_t *dst)
{
    int x, y;
    if (!alloc_like(dst, src))
        return FALSE;
    init_s_gam();
    for (y = 0; y < src->h; y++) {
        for (x = 0; x < src->w; x++) {
            const BYTE *source = src->px + (size_t)y * (size_t)src->pitch +
                                 (size_t)x * 4;
            BYTE *p = dst->px + (size_t)y * (size_t)dst->pitch +
                      (size_t)x * 4;
            float L, A, B;
            rgb_to_lab_f(source[2], source[1], source[0], &L, &A, &B);
            if (is_neutral(L, A, B)) {
                BYTE r, g, b;
                lab_to_rgb(L, A * NEU_GAIN, B * NEU_GAIN,
                           &r, &g, &b);
                p[0] = b;
                p[1] = g;
                p[2] = r;
            } else {
                p[0] = p[1] = p[2] = 0;
            }
            p[3] = 255;
        }
    }
    return TRUE;
}

static cmp_image_t *build_analysis(cmp_image_t *ci, cmp_ana_t mode)
{
    image_t derived;
    BOOL success = FALSE;
    ZeroMemory(&derived, sizeof(derived));
    if (mode == CMP_ANA_SHARP)
        success = build_sharp(&ci->img, &derived);
    else if (mode == CMP_ANA_TEXTURE)
        success = build_texture(&ci->img, &derived);
    else if (mode == CMP_ANA_NEUTRAL)
        success = build_neutral(&ci->img, &derived);
    if (!success)
        return NULL;
    {
        cmp_image_t *result = CmpImage_Adopt(&derived, ci->name);
        if (!result)
            Image_Free(&derived);
        return result;
    }
}

cmp_image_t *CmpAna_Acquire(cmp_image_t *ci, cmp_ana_t mode)
{
    cmp_image_t *derived;
    if (!ci || mode <= CMP_ANA_NONE || mode >= CMP_ANA_COUNT)
        return NULL;
    if (ci->ana[mode]) {
        ci->ana_users[mode]++;
        return ci->ana[mode];
    }
    if (ci->ana_failed[mode])
        return NULL;
    derived = build_analysis(ci, mode);
    if (!derived) {
        ci->ana_failed[mode] = TRUE;
        return NULL;
    }
    ci->ana[mode] = derived;
    ci->ana_users[mode] = 1;
    return derived;
}

void CmpAna_Release(cmp_image_t *ci, cmp_ana_t mode)
{
    if (!ci || mode <= CMP_ANA_NONE || mode >= CMP_ANA_COUNT ||
        !ci->ana[mode] || ci->ana_users[mode] <= 0)
        return;
    ci->ana_users[mode]--;
    if (ci->ana_users[mode] == 0) {
        cmp_image_t *derived = ci->ana[mode];
        ci->ana[mode] = NULL;
        CmpImage_Unref(derived);
    }
}

BOOL CmpAna_VisibleSrcImage(const cmp_image_t *ci, const cmp_view_t *view,
                            int view_w, int view_h, RECT *out)
{
    RECT visible;
    double x0, y0, x1, y1;
    if (!ci || !view || !out)
        return FALSE;
    if (!CmpView_VisibleRect(view, view_w, view_h,
                             ci->img.w, ci->img.h, &visible))
        return FALSE;
    CmpView_ScreenToImage(view, view_w, view_h,
                          (double)visible.left, (double)visible.top,
                          &x0, &y0);
    CmpView_ScreenToImage(view, view_w, view_h,
                          (double)visible.right, (double)visible.bottom,
                          &x1, &y1);
    out->left = (LONG)floor(x0);
    out->top = (LONG)floor(y0);
    out->right = (LONG)ceil(x1);
    out->bottom = (LONG)ceil(y1);
    if (out->left < 0)
        out->left = 0;
    if (out->top < 0)
        out->top = 0;
    if (out->right > ci->img.w)
        out->right = ci->img.w;
    if (out->bottom > ci->img.h)
        out->bottom = ci->img.h;
    return out->right > out->left && out->bottom > out->top;
}

BOOL CmpAna_Measure(const cmp_image_t *ci, cmp_ana_t mode,
                    const RECT *src, cmp_metric_t *out)
{
    BYTE *luma = NULL;
    DWORD *hist = NULL;
    int stride = 0, width, height;
    size_t area, step, index;
    long samples = 0, usable = 0;
    double sharp_sum = 0.0, texture_sum = 0.0;
    double neu_a_sum = 0.0, neu_b_sum = 0.0;
    if (!out)
        return FALSE;
    ZeroMemory(out, sizeof(*out));
    out->mode = mode;
    if (!ci || !src || !ci->img.valid || !ci->img.px ||
        mode <= CMP_ANA_NONE || mode >= CMP_ANA_COUNT)
        return FALSE;
    {
        RECT rc = *src;
        if (rc.left < 0) rc.left = 0;
        if (rc.top < 0) rc.top = 0;
        if (rc.right > ci->img.w) rc.right = ci->img.w;
        if (rc.bottom > ci->img.h) rc.bottom = ci->img.h;
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        if (width <= 0 || height <= 0)
            return FALSE;
        if ((double)ci->img.w * (double)ci->img.h > CMP_ANA_MAX_PIXELS)
            return FALSE;
        area = (size_t)width * (size_t)height;
        step = (area + CMP_ANA_MAX_SAMPLES - 1) / CMP_ANA_MAX_SAMPLES;
        if (step == 0)
            step = 1;
        luma = make_luma_padded(&ci->img, &stride);
        if (!luma)
            return FALSE;
        if (mode == CMP_ANA_SHARP) {
            hist = (DWORD *)calloc(2041, sizeof(*hist));
            if (!hist) {
                free(luma);
                return FALSE;
            }
        }
        for (index = 0; index < area; index += step) {
            int x = rc.left + (int)(index % (size_t)width);
            int y = rc.top + (int)(index / (size_t)width);
            const BYTE *center = luma + (size_t)(y + 2) *
                                 (size_t)stride + (size_t)(x + 2);
            samples++;
            if (mode == CMP_ANA_SHARP) {
                int g = sobel_l1(center, stride);
                hist[g]++;
                sharp_sum += g;
            } else if (mode == CMP_ANA_TEXTURE) {
                int xx, yy, sum = 0;
                int edge = sobel_l1(center, stride);
                for (yy = -2; yy <= 2; yy++)
                    for (xx = -2; xx <= 2; xx++)
                        sum += *(center + yy * stride + xx);
                if (edge < TEX_EDGE_T) {
                    texture_sum += abs(25 * (int)center[0] - sum) / 25.0;
                    usable++;
                }
            } else {
                const BYTE *p = ci->img.px + (size_t)y *
                                (size_t)ci->img.pitch + (size_t)x * 4;
                float L, A, B;
                rgb_to_lab_f(p[2], p[1], p[0], &L, &A, &B);
                if (is_neutral(L, A, B)) {
                    neu_a_sum += A;
                    neu_b_sum += B;
                    usable++;
                }
            }
        }
    }
    out->samples = samples;
    if (samples <= 0)
        goto done;
    if (mode == CMP_ANA_SHARP) {
        DWORD target = (DWORD)ceil((double)samples * 0.99);
        DWORD cumulative = 0;
        int g;
        if (target < 1)
            target = 1;
        for (g = 0; g <= 2040; g++) {
            cumulative += hist[g];
            if (cumulative >= target)
                break;
        }
        out->sharp_p99 = (float)g;
        out->sharp_mean = (float)(sharp_sum / samples);
    } else if (mode == CMP_ANA_TEXTURE) {
        out->tex_cover = (float)(100.0 * usable / samples);
        if (usable > 0)
            out->tex_mean = (float)(texture_sum / usable);
    } else {
        out->neu_cover = (float)(100.0 * usable / samples);
        if (usable > 0) {
            out->neu_a = (float)(neu_a_sum / usable);
            out->neu_b = (float)(neu_b_sum / usable);
            out->neu_cast = sqrtf(out->neu_a * out->neu_a +
                                  out->neu_b * out->neu_b);
            out->neu_hue = (float)(atan2(out->neu_b, out->neu_a) *
                                   (180.0 / 3.14159265358979323846));
            if (out->neu_hue < 0.0f)
                out->neu_hue += 360.0f;
        }
    }
    out->valid = TRUE;
done:
    free(hist);
    free(luma);
    return out->valid;
}

const char *CmpAna_HueName(float deg)
{
    static const char *names[8] = {
        "Red", "Orange", "Yellow", "Yel-Grn",
        "Green", "Cyan", "Blue", "Magenta"
    };
    int sector;
    while (deg < 0.0f)
        deg += 360.0f;
    while (deg >= 360.0f)
        deg -= 360.0f;
    sector = (int)floor((deg + 22.5f) / 45.0f) % 8;
    return names[sector];
}

int CmpAna_Format(const cmp_metric_t *m, char *buf, int cch)
{
    if (!buf || cch <= 0)
        return 0;
    buf[0] = '\0';
    if (!m || !m->valid) {
        _snprintf(buf, (size_t)cch, "n/a");
    } else if (m->mode == CMP_ANA_SHARP) {
        _snprintf(buf, (size_t)cch, "P99 %.0f | mean %.1f",
                  m->sharp_p99, m->sharp_mean);
    } else if (m->mode == CMP_ANA_TEXTURE) {
        _snprintf(buf, (size_t)cch, "texture %.1f | %.0f%%",
                  m->tex_mean, m->tex_cover);
    } else if (m->mode == CMP_ANA_NEUTRAL) {
        _snprintf(buf, (size_t)cch,
                  "neutral %.0f%% | a %.1f b %.1f | cast %.1f %s",
                  m->neu_cover, m->neu_a, m->neu_b, m->neu_cast,
                  m->neu_cast < 1.0f ? "none" : CmpAna_HueName(m->neu_hue));
    } else {
        _snprintf(buf, (size_t)cch, "n/a");
    }
    buf[cch - 1] = '\0';
    return (int)strlen(buf);
}

static BOOL ana_better(const cmp_metric_t *a, const cmp_metric_t *b)
{
    if (!b->valid)
        return TRUE;
    if (a->mode == CMP_ANA_SHARP) {
        if (a->sharp_p99 != b->sharp_p99)
            return a->sharp_p99 > b->sharp_p99;
        return a->sharp_mean > b->sharp_mean;
    }
    if (a->mode == CMP_ANA_TEXTURE)
        return a->tex_mean > b->tex_mean;
    if (a->mode == CMP_ANA_NEUTRAL)
        return a->neu_cast < b->neu_cast;
    return FALSE;
}

int CmpAna_BestIndex(const cmp_metric_t *m, int n)
{
    int i, best = -1, valid = 0;
    if (!m || n <= 0)
        return -1;
    for (i = 0; i < n; i++) {
        if (!m[i].valid)
            continue;
        valid++;
        if (best < 0 || ana_better(&m[i], &m[best]))
            best = i;
    }
    return valid >= 2 ? best : -1;
}
