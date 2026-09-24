#include "log.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "analyze.h"

static const char *kHeader =
    "timestamp,image,mode,x0,y0,x1,y1,count,"
    "R_mean,R_std,G_mean,G_std,B_mean,B_std,Y_mean,Y_std,L_mean,a_mean,b_mean\n";

/* exe dir + "\\roi_log.<ext>" */
static void log_path(char *dst, size_t cap, const char *ext)
{
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        snprintf(dst, cap, "bin\\roi_log.%s", ext);
        return;
    }
    /* Strip file name -> dir; logs live next to the exe (bin/). */
    while (n > 0 && dir[n - 1] != '\\' && dir[n - 1] != '/')
        n--;
    dir[n] = '\0';
    snprintf(dst, cap, "%sroi_log.%s", dir, ext);
}

static const char *base_name(const char *path)
{
    const char *p = path ? path : "";
    const char *s1 = strrchr(p, '\\');
    const char *s2 = strrchr(p, '/');
    const char *b = p;

    if (s1 && s1 + 1 > b)
        b = s1 + 1;
    if (s2 && s2 + 1 > b)
        b = s2 + 1;
    return b;
}

/* -0.00 -> +0.00 so CSV float-compare stays clean. */
static double no_neg_zero(double v)
{
    return (v == 0.0) ? 0.0 : v;
}

int LogROI(const char *img_path, const char *mode, const roi_result_t *r)
{
    char csv_path[MAX_PATH], txt_path[MAX_PATH];
    char ts_compact[40], ts_human[40];
    SYSTEMTIME st;
    FILE *f;

    if (!mode || !r)
        return -1;

    GetLocalTime(&st);
    snprintf(ts_compact, sizeof(ts_compact), "%04d-%02d-%02d_%02d-%02d-%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    snprintf(ts_human, sizeof(ts_human), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    log_path(csv_path, sizeof(csv_path), "csv");
    log_path(txt_path, sizeof(txt_path), "txt");

    f = fopen(csv_path, "a");
    if (!f)
        return -1;
    /* Fresh file (or user-deleted-then-empty): write the 19-col header. */
    if (ftell(f) == 0)
        fputs(kHeader, f);
    fprintf(f, "%s,%s,%s,%d,%d,%d,%d,%d,"
               "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
            ts_compact, base_name(img_path), mode,
            r->x0, r->y0, r->x1, r->y1, r->count,
            no_neg_zero(r->r_mean), no_neg_zero(r->r_std),
            no_neg_zero(r->g_mean), no_neg_zero(r->g_std),
            no_neg_zero(r->b_mean), no_neg_zero(r->b_std),
            no_neg_zero(r->y_mean), no_neg_zero(r->y_std),
            no_neg_zero(r->lab_l), no_neg_zero(r->lab_a), no_neg_zero(r->lab_b));
    fclose(f);

    f = fopen(txt_path, "a");
    if (!f)
        return -1;
    fprintf(f, "[%s] %s mode=%s rect=(%d,%d)-(%d,%d) count=%d\n",
            ts_human, base_name(img_path), mode,
            r->x0, r->y0, r->x1, r->y1, r->count);
    fprintf(f, "  R: mean=%.2f std=%.2f   G: mean=%.2f std=%.2f   B: mean=%.2f std=%.2f\n",
            no_neg_zero(r->r_mean), no_neg_zero(r->r_std),
            no_neg_zero(r->g_mean), no_neg_zero(r->g_std),
            no_neg_zero(r->b_mean), no_neg_zero(r->b_std));
    fprintf(f, "  Y: mean=%.2f std=%.2f\n",
            no_neg_zero(r->y_mean), no_neg_zero(r->y_std));
    fprintf(f, "  Lab: L=%.2f a=%.2f b=%.2f\n",
            no_neg_zero(r->lab_l), no_neg_zero(r->lab_a), no_neg_zero(r->lab_b));
    fclose(f);
    return 0;
}

int Log_Clear(void)
{
    char csv_path[MAX_PATH], txt_path[MAX_PATH];

    log_path(csv_path, sizeof(csv_path), "csv");
    log_path(txt_path, sizeof(txt_path), "txt");
    DeleteFileA(csv_path);
    DeleteFileA(txt_path);
    return 0;
}
