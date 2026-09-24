#ifndef ROI_ROI_H
#define ROI_ROI_H

#include <windows.h>

struct view_s;
typedef struct view_s view_t;
struct image_s;
typedef struct image_s image_t;

typedef enum { MODE_DRAG, MODE_FIX3, MODE_FIX5 } roi_mode_t;

typedef struct {
    roi_mode_t mode;
    BOOL dragging;      // rubber-banding (MODE_DRAG only)
    POINT anchor;       // drag start (IMAGE coords, converted at LDown)
    RECT rubber;        // rubber band (IMAGE coords inclusive: left/top = anchor,
                        // right/bottom = current; normalized at draw time)
    RECT confirmed;     // confirmed box, IMAGE coords inclusive: left=x0 top=y0 right=x1 bottom=y1
    BOOL has_confirmed;
    POINT preview;      // fixed-mode preview center (image coords)
    BOOL has_preview;
} roi_state_t;

void ROI_Init(roi_state_t *s);
void ROI_Clear(roi_state_t *s); // forget confirmed + preview + dragging
const char *ROI_ModeStr(roi_mode_t m);   // "drag" / "3x3" / "5x5" (log)
const char *ROI_ModeTitle(roi_mode_t m); // "Drag" / "3x3" / "5x5" (title bar)
int ROI_FixRadius(roi_mode_t m);         // 0 for DRAG, 1 for FIX3, 2 for FIX5

// All take window-coord point; on confirm return TRUE and fill *out_img (inclusive).
BOOL ROI_OnLDown(roi_state_t *s, const view_t *v, const image_t *img, POINT wp, RECT *out_img);
BOOL ROI_OnMove(roi_state_t *s, const view_t *v, const image_t *img, POINT wp);
BOOL ROI_OnLUp(roi_state_t *s, const view_t *v, const image_t *img, POINT wp, RECT *out_img);

void ROI_DrawOverlay(HDC hdc, const view_t *v, const roi_state_t *s);

#endif
