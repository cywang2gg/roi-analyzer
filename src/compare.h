#ifndef ROI_COMPARE_H
#define ROI_COMPARE_H

#include "image.h"
#include "view.h"
#include <stddef.h>

#define CMP_ZOOM_MIN      0.02
#define CMP_ZOOM_MAX      8.0
#define CMP_ZOOM_STEP     1.15
#define CMP_MAX_CELLS     4
#define CMP_MAX_WINDOWS   8
#define CMP_MAX_LIVE_IMG  8
#define CMPM_KEY          (WM_APP + 0x40)
#define CMPM_METRICS      (WM_APP + 0x41)
#define CMP_ID_METRICS    109
#define CMP_RENDER_SNAPSHOT 0x01

typedef struct metrics_async_job metrics_async_job_t;

typedef struct cmp_image {
    image_t img;
    view_pyr_t pyr;
    BOOL pyr_built;
    BOOL pyr_failed;
    volatile LONG refs;
    char name[MAX_PATH];
} cmp_image_t;

typedef struct {
    double zoom;
    double u;
    double v;
} cmp_view_t;

typedef struct {
    const BYTE *px;
    int w;
    int h;
    int pitch;
    int shift;
} cmp_level_t;

typedef struct {
    UINT input;
    UINT directories;
    UINT unsupported;
    UINT non_acp;
    UINT duplicates;
    UINT collected;
    UINT truncated;
    BOOL allocation_failed;
} cmp_drop_stats_t;

typedef struct {
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    int width;
    int height;
} cmp_snap_t;

typedef enum {
    CMP_OPEN_WITH_CURRENT = 0,
    CMP_OPEN_MAIN_FIRST = 1
} cmp_open_mode_t;

BOOL Compare_Init(HINSTANCE instance, HFONT font);
BOOL Compare_CanOpen(int need);
void Compare_CloseAll(void);
BOOL Compare_PreTranslate(MSG *msg);
HWND CompareV1_Open(cmp_image_t **images, int count);
HWND CompareV2_Open(cmp_image_t *left, cmp_image_t *right);

cmp_image_t *CmpImage_Load(const char *path);
cmp_image_t *CmpImage_FromImage(const image_t *src);
cmp_image_t *CmpImage_Ref(cmp_image_t *image);
void CmpImage_Unref(cmp_image_t *image);
LONG CmpImage_LiveCount(void);
int CmpImage_Levels(cmp_image_t *image);
BOOL CmpImage_Level(cmp_image_t *image, int k, cmp_level_t *out);

double CmpZoom_Quantize(double zoom);
double CmpZoom_Step(double zoom, int direction);
void CmpView_Fit(cmp_view_t *view, int image_w, int image_h,
                 int view_w, int view_h, double margin);
void CmpView_ZoomAt(cmp_view_t *view, double zoom, double x, double y,
                    int view_w, int view_h);
void CmpView_Pan(cmp_view_t *view, double dx, double dy);
void CmpView_ClampEdges(cmp_view_t *view, int image_w, int image_h,
                        int view_w, int view_h);
void CmpView_ScreenToImage(const cmp_view_t *view, int view_w, int view_h,
                           double x, double y, double *image_x,
                           double *image_y);
void Cmp_Blit(HDC dc, cmp_image_t *image, const cmp_view_t *view,
              const RECT *viewport, const RECT *clip);
BOOL CmpView_VisibleRect(const cmp_view_t *view, int view_w, int view_h,
                         int image_w, int image_h, RECT *visible);
BOOL CmpDrop_Collect(HDROP drop, char paths[CMP_MAX_CELLS][MAX_PATH],
                     int capacity, cmp_drop_stats_t *stats);
BOOL wide_to_acp_strict(const wchar_t *wide, char *path, size_t capacity);
HFONT Cmp_UiFont(void);
void CmpInfo_Add(char lines[CMP_MAX_CELLS + 1][256], int *count,
                 const char *text);
int CmpInfo_Height(HFONT font, int lines);
void CmpInfo_Draw(HDC dc, int width, int top, HFONT font,
                  char lines[CMP_MAX_CELLS + 1][256], int count);
BOOL CmpSnap_Begin(HWND owner, int width, int height, cmp_snap_t *snapshot);
void CmpSnap_Finalize(cmp_snap_t *snapshot);
void CmpSnap_End(cmp_snap_t *snapshot);
double Snap_PhysicalScale(HWND hwnd);
int Snap_Round(double value);
HFONT CmpSnap_CreateScaledFont(double scale);
BOOL CmpSnap_MakePath(const SYSTEMTIME *time, const char *ref_path,
                      const char *name_a, const char *name_b,
                      char *path, size_t capacity);
BOOL CmpSnap_SavePng(HBITMAP bitmap, char *path, size_t capacity,
                     BOOL overwrite);
BOOL CmpSnap_CopyToClipboard(HWND owner, HBITMAP bitmap);
/* Returns clipboard-copy success; path is non-empty only when saved. */
BOOL CmpSnap_Deliver(HWND owner, HBITMAP bitmap, const SYSTEMTIME *time,
                     const char *ref_path, const char *name_a,
                     const char *name_b, char *path, size_t capacity);
BOOL cmp_save_as_dialog(HWND owner, char *path, size_t capacity);

BOOL CmpReg_Add(HWND hwnd);
void CmpReg_Remove(HWND hwnd);
HFONT Compare_Font(void);
BOOL Compare_RunMetrics(HWND hwnd, cmp_image_t *const *images,
                        const RECT *roi_rects, int count,
                        metrics_async_job_t **async_job);
BOOL Compare_CompleteMetrics(metrics_async_job_t *job, BOOL *cancelled);
BOOL Compare_TriggerMetrics(void);
BOOL Compare_MetricsStage2Enabled(void);
void Compare_SetMetricsStage2Enabled(BOOL enabled);

#endif
