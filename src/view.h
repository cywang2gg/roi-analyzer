#ifndef ROI_VIEW_H
#define ROI_VIEW_H

#include <windows.h>

struct image_s;
typedef struct image_s image_t;

// Fit-to-window letterbox params (image centered).
typedef struct view_s {
    int off_x, off_y;   // image origin in window coords
    int draw_w, draw_h; // image draw size
    float scale;        // draw_w / image_w
} view_t;

// Recompute from client size (cw/ch) and image size (iw/ih). Zero-safe.
void View_Update(view_t *v, int cw, int ch, int iw, int ih);

// Window -> image coords. Returns FALSE when outside image (ip untouched).
BOOL View_ToImage(const view_t *v, int iw, int ih, POINT wp, POINT *ip);

// Image -> window coords.
void View_ToWindow(const view_t *v, POINT ip, POINT *wp);

// Paint image scaled into view rect (no-op when invalid).
void View_DrawImage(HDC hdc, const view_t *v, const image_t *img);

#endif
