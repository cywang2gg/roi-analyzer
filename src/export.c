#include "export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_suffix(roi_mode_t mode)
{
    switch (mode) {
    case MODE_GRID3:
        return "grid3x3";
    case MODE_GRID5:
        return "grid5x5";
    case MODE_DRAG:
    default:
        return "drag";
    }
}

int Export_GetPath(const image_t *img, roi_mode_t mode, char *dst, size_t cap)
{
    const char *base, *slash1, *slash2, *dot;
    size_t dir_len, base_len;
    int written;

    if (!dst || cap == 0)
        return -1;
    dst[0] = '\0';
    if (!img || !img->valid || !img->path[0])
        return -1;
    base = img->path;
    slash1 = strrchr(img->path, '\\');
    slash2 = strrchr(img->path, '/');
    if (slash1 && slash1 + 1 > base)
        base = slash1 + 1;
    if (slash2 && slash2 + 1 > base)
        base = slash2 + 1;
    dot = strrchr(base, '.');
    base_len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    dir_len = (size_t)(base - img->path);
    written = _snprintf(dst, cap, "%.*s%.*s_%s.log",
                        (int)dir_len, img->path, (int)base_len, base,
                        mode_suffix(mode));
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

static int write_utf8(FILE *file, const char *text)
{
    int wchars, bytes;
    wchar_t *wide;
    char *utf8;
    int ok = -1;

    wchars = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (wchars <= 0)
        return -1;
    wide = (wchar_t *)malloc((size_t)wchars * sizeof(*wide));
    if (!wide)
        return -1;
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide, wchars) <= 0)
        goto done;
    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        goto done;
    utf8 = (char *)malloc((size_t)bytes);
    if (!utf8)
        goto done;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, bytes, NULL, NULL) > 0 &&
        fwrite(utf8, 1, (size_t)bytes - 1, file) == (size_t)bytes - 1)
        ok = 0;
    free(utf8);
done:
    free(wide);
    return ok;
}

static double clean_zero(double value)
{
    return value == 0.0 ? 0.0 : value;
}

int Export_Log(const image_t *img, const roi_list_t *rois)
{
    char path[MAX_PATH];
    char line[1024];
    SYSTEMTIME now;
    int source_index, i, failed = 0;
    static const roi_mode_t modes[] = { MODE_DRAG, MODE_GRID3, MODE_GRID5 };
    static const roi_source_t sources[] = {
        ROI_SRC_MANUAL, ROI_SRC_GRID3, ROI_SRC_GRID5
    };

    if (!img || !img->valid || !rois)
        return -1;
    for (source_index = 0; source_index < 3 && !failed; source_index++) {
        FILE *file;
        int count = ROI_SourceCount(rois, sources[source_index]);
        int number = 0;
        if (count == 0)
            continue;
        if (Export_GetPath(img, modes[source_index], path, sizeof(path)) != 0) {
            failed = 1;
            break;
        }
        file = fopen(path, "ab");
        if (!file) {
            failed = 1;
            break;
        }
        GetLocalTime(&now);
        _snprintf(line, sizeof(line),
                  "# ==== export [%04u-%02u-%02u %02u:%02u:%02u] image=%s size=%dx%d mode=%s rois=%d\r\n",
                  (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
                  (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond,
                  img->path, img->w, img->h, mode_suffix(modes[source_index]), count);
        line[sizeof(line) - 1] = '\0';
        if (write_utf8(file, line) != 0)
            failed = 1;
        for (i = 0; i < rois->count && !failed; i++) {
            const roi_result_t *r;
            if (rois->items[i].source != sources[source_index])
                continue;
            number++;
            r = &rois->items[i].res;
            _snprintf(line, sizeof(line),
                      "# roi %d rect=(%d,%d)-(%d,%d) count=%d\r\n",
                      number, r->x0, r->y0, r->x1, r->y1, r->count);
            line[sizeof(line) - 1] = '\0';
            if (write_utf8(file, line) != 0) {
                failed = 1;
                break;
            }
            _snprintf(line, sizeof(line),
                      "RGB mean=(%.2f,%.2f,%.2f) std=(%.2f,%.2f,%.2f)\r\n",
                      clean_zero(r->r_mean), clean_zero(r->g_mean), clean_zero(r->b_mean),
                      clean_zero(r->r_std), clean_zero(r->g_std), clean_zero(r->b_std));
            line[sizeof(line) - 1] = '\0';
            if (write_utf8(file, line) != 0) {
                failed = 1;
                break;
            }
            _snprintf(line, sizeof(line), "Y mean=%.2f std=%.2f\r\n",
                      clean_zero(r->y_mean), clean_zero(r->y_std));
            line[sizeof(line) - 1] = '\0';
            if (write_utf8(file, line) != 0) {
                failed = 1;
                break;
            }
            _snprintf(line, sizeof(line), "Lab L=%.2f a=%.2f b=%.2f\r\n",
                      clean_zero(r->lab_l), clean_zero(r->lab_a), clean_zero(r->lab_b));
            line[sizeof(line) - 1] = '\0';
            if (write_utf8(file, line) != 0)
                failed = 1;
        }
        if (fclose(file) != 0)
            failed = 1;
    }
    return failed ? -1 : 0;
}
