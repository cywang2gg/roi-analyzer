#include "export.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

static const unsigned char k_utf16le_bom[] = { 0xFF, 0xFE };
static const char k_column_header[] =
    "id\tRm\tRs\tGm\tGs\tBm\tBs\tYm\tYs\tL\ta\tb\trect\tcount\r\n";

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
    const char *base, *extension;
    size_t dir_len, base_len;
    int written;

    if (!dst || cap == 0)
        return -1;
    dst[0] = '\0';
    if (!img || !img->valid || !img->path[0])
        return -1;
    base = PathFindFileNameA(img->path);
    if (!base)
        return -1;
    extension = PathFindExtensionA(base);
    if (!extension || extension == base)
        extension = base + strlen(base);
    base_len = (size_t)(extension - base);
    dir_len = (size_t)(base - img->path);
    written = _snprintf(dst, cap, "%.*s%.*s_%s.csv",
                        (int)dir_len, img->path, (int)base_len, base,
                        mode_suffix(mode));
    dst[cap - 1] = '\0';
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

static int write_utf16le(FILE *file, const char *text)
{
    int wlen;
    WCHAR *wbuf;
    size_t i;
    int ok = -1;

    wlen = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (wlen <= 0)
        return -1;
    wbuf = (WCHAR *)malloc((size_t)wlen * sizeof(*wbuf));
    if (wbuf == NULL)
        return -1;
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, wlen) <= 0)
        goto done;
    for (i = 0; i + 1 < (size_t)wlen; i++) {
        unsigned char lo;
        unsigned char hi;

        lo = (unsigned char)(wbuf[i] & 0xFF);
        hi = (unsigned char)((wbuf[i] >> 8) & 0xFF);
        if (fputc(lo, file) == EOF || fputc(hi, file) == EOF)
            goto done;
    }
    ok = 0;
done:
    free(wbuf);
    return ok;
}

static double clean_zero(double value)
{
    return value == 0.0 ? 0.0 : value;
}

int Export_Log(const image_t *img, const roi_list_t *rois,
               char *error_path, size_t error_path_cap)
{
    char path[MAX_PATH];
    char line[1024];
    SYSTEMTIME now;
    int source_index, i, failed = 0, export_error = 0;
    static const roi_mode_t modes[] = { MODE_DRAG, MODE_GRID3, MODE_GRID5 };
    static const roi_source_t sources[] = {
        ROI_SRC_MANUAL, ROI_SRC_GRID3, ROI_SRC_GRID5
    };

    errno = 0;
    if (error_path && error_path_cap > 0)
        error_path[0] = '\0';
    if (!img || !img->valid || !rois)
        return -1;
    for (source_index = 0; source_index < 3 && !failed; source_index++) {
        FILE *file;
        long file_size;
        int count = ROI_SourceCount(rois, sources[source_index]);
        int number = 0;
        if (count == 0)
            continue;
        if (Export_GetPath(img, modes[source_index], path, sizeof(path)) != 0) {
            failed = 1;
            break;
        }
        if (error_path && error_path_cap > 0) {
            size_t path_length;

            path_length = strlen(path);
            if (path_length < error_path_cap)
                memcpy(error_path, path, path_length + 1);
            else
                error_path[0] = '\0';
        }
        file = fopen(path, "a+b");
        if (!file) {
            export_error = errno;
            failed = 1;
            break;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            failed = 1;
        } else {
            file_size = ftell(file);
            if (file_size < 0) {
                failed = 1;
            } else if (file_size == 0) {
                if (fwrite(k_utf16le_bom, 1, sizeof(k_utf16le_bom), file) !=
                    sizeof(k_utf16le_bom))
                    failed = 1;
            } else if (file_size == 1) {
                export_error = EINVAL;
                failed = 1;
            } else {
                unsigned char bom[2];

                if (fseek(file, 0, SEEK_SET) != 0) {
                    failed = 1;
                } else if (fread(bom, 1, sizeof(bom), file) != sizeof(bom)) {
                    failed = 1;
                } else if (bom[0] != k_utf16le_bom[0] ||
                           bom[1] != k_utf16le_bom[1]) {
                    export_error = EILSEQ;
                    failed = 1;
                } else if (fseek(file, 0, SEEK_END) != 0) {
                    failed = 1;
                } else if (write_utf16le(file, "\r\n") != 0) {
                    failed = 1;
                }
            }
        }
        GetLocalTime(&now);
        if (!failed) {
            int written;
            written = _snprintf(line, sizeof(line),
                                "# ==== export [%04u-%02u-%02u %02u:%02u:%02u] image=%s size=%dx%d mode=%s rois=%d\r\n",
                                (unsigned)now.wYear, (unsigned)now.wMonth,
                                (unsigned)now.wDay, (unsigned)now.wHour,
                                (unsigned)now.wMinute, (unsigned)now.wSecond,
                                img->path, img->w, img->h,
                                mode_suffix(modes[source_index]), count);
            line[sizeof(line) - 1] = '\0';
            if (written < 0 || (size_t)written >= sizeof(line) ||
                write_utf16le(file, line) != 0)
                failed = 1;
        }
        if (!failed && write_utf16le(file, k_column_header) != 0)
            failed = 1;
        for (i = 0; i < rois->count && !failed; i++) {
            const roi_result_t *r;
            int written;
            if (rois->items[i].source != sources[source_index])
                continue;
            number++;
            r = &rois->items[i].res;
            written = _snprintf(line, sizeof(line),
                                "%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t(%d,%d)-(%d,%d)\t%d\r\n",
                                number,
                                clean_zero(r->r_mean), clean_zero(r->r_std),
                                clean_zero(r->g_mean), clean_zero(r->g_std),
                                clean_zero(r->b_mean), clean_zero(r->b_std),
                                clean_zero(r->y_mean), clean_zero(r->y_std),
                                clean_zero(r->lab_l), clean_zero(r->lab_a),
                                clean_zero(r->lab_b),
                                (int)r->x0, (int)r->y0,
                                (int)r->x1, (int)r->y1, r->count);
            line[sizeof(line) - 1] = '\0';
            if (written < 0 || (size_t)written >= sizeof(line) ||
                write_utf16le(file, line) != 0)
                failed = 1;
        }
        if (fclose(file) != 0)
            failed = 1;
    }
    errno = export_error;
    if (!failed && error_path && error_path_cap > 0)
        error_path[0] = '\0';
    return failed ? -1 : 0;
}
