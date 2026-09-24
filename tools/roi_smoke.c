// Smoke test: load PNG via GDI+ flat path, AnalyzeROI full-image, LogROI append.
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <gdiplus/gdiplus.h>
#include "image.h"
#include "analyze.h"
#include "log.h"

int main(int argc, char **argv) {
    GdiplusStartupInput gsi = { 1, NULL, FALSE, FALSE };
    ULONG_PTR tok = 0;
    image_t img;
    roi_result_t r;
    RECT rc;

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
    if (LogROI(img.path, "drag", &r) != 0) { printf("LOG FAIL\n"); return 1; }
    printf("logged ok\n");
    Image_Free(&img);
    GdiplusShutdown(tok);
    return 0;
}
