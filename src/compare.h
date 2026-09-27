#ifndef ROI_COMPARE_H
#define ROI_COMPARE_H

#include "image.h"
#include "view.h"

#define CMP_ZOOM_MIN      0.02
#define CMP_ZOOM_MAX      8.0
#define CMP_ZOOM_STEP     1.15
#define CMP_MAX_CELLS     4
#define CMP_MAX_WINDOWS   8
#define CMP_MAX_LIVE_IMG  8
#define CMPM_KEY          (WM_APP + 0x40)

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

BOOL CmpReg_Add(HWND hwnd);
void CmpReg_Remove(HWND hwnd);
HFONT Compare_Font(void);

#endif
