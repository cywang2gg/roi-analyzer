// Smoke test: verify source-directory, per-mode append-only ROI logs.
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <gdiplus/gdiplus.h>
#include "image.h"
#include "analyze.h"
#include "log.h"

static int count_headers(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "# roi rect=", 11) == 0)
            n++;
    fclose(f);
    return n;
}

int main(int argc, char **argv) {
    GdiplusStartupInput gsi = { 1, NULL, FALSE, FALSE };
    ULONG_PTR tok = 0;
    image_t img;
    roi_result_t r;
    RECT rc;
    char drag_path[MAX_PATH] = { 0 };
    char fix3_path[MAX_PATH] = { 0 };
    char legacy_path[MAX_PATH] = { 0 };

    if (argc < 2) { printf("usage: roi_smoke <img>\n"); return 2; }
    memset(&img, 0, sizeof(img));
    memset(&r, 0, sizeof(r));
    if (GdiplusStartup(&tok, &gsi, NULL) != 0) { printf("gdiplus fail\n"); return 1; }
    if (Image_Load(&img, argv[1]) != 0) { printf("LOAD FAIL %s\n", argv[1]); return 1; }
    printf("loaded %dx%d pitch=%d\n", img.w, img.h, img.pitch);
    printf("px0 B=%d G=%d R=%d\n", img.px[0], img.px[1], img.px[2]);
    rc.left = 0; rc.top = 0; rc.right = img.w - 1; rc.bottom = img.h - 1;
    AnalyzeROI(&img, rc, &r);
    printf("count=%d R=%.2f/%.2f G=%.2f/%.2f B=%.2f/%.2f Y=%.2f/%.2f Lab=%.2f/%.2f/%.2f\n",
           r.count, r.r_mean, r.r_std, r.g_mean, r.g_std, r.b_mean, r.b_std,
           r.y_mean, r.y_std, r.lab_l, r.lab_a, r.lab_b);
    Log_GetPath(img.path, "drag", drag_path, sizeof(drag_path));
    Log_GetPath(img.path, "3x3", fix3_path, sizeof(fix3_path));
    snprintf(legacy_path, sizeof(legacy_path), "bin\\%s_log.log", "test_red");
    DeleteFileA(drag_path);
    DeleteFileA(fix3_path);
    DeleteFileA(legacy_path);
    if (LogROI(img.path, "drag", &r) != 0 || LogROI(img.path, "drag", &r) != 0) {
        printf("DRAG LOG FAIL\n"); return 1;
    }
    if (count_headers(drag_path) != 2) { printf("APPEND LOG FAIL\n"); return 1; }
    if (LogROI(img.path, "3x3", &r) != 0 || count_headers(fix3_path) != 1) {
        printf("FIX3 LOG FAIL\n"); return 1;
    }
    if (GetFileAttributesA(legacy_path) != INVALID_FILE_ATTRIBUTES) {
        printf("LEGACY LOG PRESENT\n"); return 1;
    }
    printf("drag_log=%s\nfix3_log=%s\nlogged ok\n", drag_path, fix3_path);
    Image_Free(&img);
    GdiplusShutdown(tok);
    return 0;
}
