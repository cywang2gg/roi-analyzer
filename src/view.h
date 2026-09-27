#ifndef ROI_VIEW_H
#define ROI_VIEW_H

#include <windows.h>

struct image_s;
typedef struct image_s image_t;

// Fit-to-window letterbox params with a relative zoom (image centered at fit).
typedef struct view_s {
    int off_x, off_y;   // image origin in window coords
    int draw_w, draw_h; // image draw size
    int pan_x, pan_y;   // user panning offset from centered position
    float scale;        // draw_w / image_w
    float zoom;         // multiplier of fit scale; 1.0 means fit
} view_t;

#define VIEW_MAX_LEVELS 6

typedef struct {
    unsigned char *px;
    int w, h, pitch;
    BOOL owned;
} view_level_t;

typedef struct {
    view_level_t levels[VIEW_MAX_LEVELS];
    int count;
} view_pyr_t;

BOOL ViewPyr_Build(view_pyr_t *pyramid, const image_t *img);
void ViewPyr_Free(view_pyr_t *pyramid);
const view_level_t *ViewPyr_Pick(const view_pyr_t *pyramid,
                                 int draw_w, int draw_h, BOOL nearest);

// Recompute from client and image sizes at the requested relative zoom.
void View_Update(view_t *v, int cw, int ch, int iw, int ih, float zoom);

// Add a canvas-space pan delta and recompute the clamped view.
void View_Pan(view_t *v, const image_t *img, int cw, int ch, int dx, int dy);

// Reset to fit zoom and centered position.
void View_Reset(view_t *v, const image_t *img, int cw, int ch);

// Change zoom while preserving the image coordinate under the window anchor.
void View_SetZoom(view_t *v, const image_t *img, int cw, int ch,
                  float new_zoom, POINT anchor);

// Window -> image coords. Returns FALSE when outside image (ip untouched).
BOOL View_ToImage(const view_t *v, int iw, int ih, POINT wp, POINT *ip);

// Image -> window coords.
void View_ToWindow(const view_t *v, POINT ip, POINT *wp);

// Image-coordinate inclusive rectangle to window-coordinate exclusive edges.
void View_RectToWindow(const view_t *v, RECT image_rect, RECT *window_rect);

// Paint image scaled into view rect (no-op when invalid).
void View_DrawImage(HDC hdc, const view_t *v, const image_t *img);
void View_DrawImagePyramid(HDC hdc, const view_t *v, const image_t *img,
                           const view_pyr_t *pyramid);

#endif
