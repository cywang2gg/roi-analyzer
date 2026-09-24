#include "log.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static void log_path(const char *img_path, const char *mode, char *dst, size_t cap)
{
    char dir[MAX_PATH], name[MAX_PATH], suffix[8];
    const char *base, *slash, *dot;
    size_t name_len, dir_len;

    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!img_path || !img_path[0] || !mode)
        return;
    base = img_path;
    slash = strrchr(img_path, '\\');
    if (slash && slash + 1 > base)
        base = slash + 1;
    slash = strrchr(img_path, '/');
    if (slash && slash + 1 > base)
        base = slash + 1;
    dot = strrchr(base, '.');
    name_len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (name_len >= sizeof(name) - 9)
        name_len = sizeof(name) - 10;
    memcpy(name, base, name_len);
    name[name_len] = '\0';
    if (strcmp(mode, "3x3") == 0)
        strcpy(suffix, "3x3");
    else if (strcmp(mode, "5x5") == 0)
        strcpy(suffix, "5x5");
    else if (strcmp(mode, "drag") == 0)
        strcpy(suffix, "drag");
    else
        return;
    dir_len = (size_t)(base - img_path);
    if (dir_len >= sizeof(dir))
        return;
    memcpy(dir, img_path, dir_len);
    dir[dir_len] = '\0';
    snprintf(dst, cap, "%s%s_%s.log", dir, name, suffix);
}

static double no_neg_zero(double v)
{
    return (v == 0.0) ? 0.0 : v;
}

int LogROI(const char *img_path, const char *mode, const roi_result_t *r)
{
    char path[MAX_PATH], timestamp[40];
    SYSTEMTIME st;
    FILE *f;

    if (!img_path || !img_path[0] || !mode || !r)
        return -1;
    log_path(img_path, mode, path, sizeof(path));
    if (!path[0])
        return -1;
    GetLocalTime(&st);
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    f = fopen(path, "a");
    if (!f)
        return -1;
    fprintf(f, "# roi rect=(%d,%d)-(%d,%d) count=%d mode=%s [%s]\n",
            r->x0, r->y0, r->x1, r->y1, r->count, mode, timestamp);
    fprintf(f, "RGB mean=(%.2f,%.2f,%.2f) std=(%.2f,%.2f,%.2f)\n",
            no_neg_zero(r->r_mean), no_neg_zero(r->g_mean), no_neg_zero(r->b_mean),
            no_neg_zero(r->r_std), no_neg_zero(r->g_std), no_neg_zero(r->b_std));
    fprintf(f, "Y mean=%.2f std=%.2f\n", no_neg_zero(r->y_mean), no_neg_zero(r->y_std));
    fprintf(f, "Lab L=%.2f a=%.2f b=%.2f\n",
            no_neg_zero(r->lab_l), no_neg_zero(r->lab_a), no_neg_zero(r->lab_b));
    return fclose(f) == 0 ? 0 : -1;
}

int Log_Clear(const char *img_path, const char *mode)
{
    char path[MAX_PATH];

    log_path(img_path, mode, path, sizeof(path));
    if (!path[0])
        return -1;
    DeleteFileA(path);
    return 0;
}

void Log_GetPath(const char *img_path, const char *mode, char *dst, size_t cap)
{
    log_path(img_path, mode, dst, cap);
}
