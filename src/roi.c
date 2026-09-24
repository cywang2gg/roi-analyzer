#include "roi.h"

#include <string.h>

#include "image.h"
#include "view.h"

void ROI_Init(roi_state_t *s)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->mode = MODE_DRAG;
}

void ROI_Clear(roi_state_t *s)
{
    roi_mode_t m;

    if (!s)
        return;
    m = s->mode;
    memset(s, 0, sizeof(*s));
    s->mode = m;
}

const char *ROI_ModeStr(roi_mode_t m)
{
    switch (m) {
    case MODE_FIX3:
        return "3x3";
    case MODE_FIX5:
        return "5x5";
    case MODE_DRAG:
    default:
        return "drag";
    }
}

const char *ROI_ModeTitle(roi_mode_t m)
{
    switch (m) {
    case MODE_FIX3:
        return "3x3";
    case MODE_FIX5:
        return "5x5";
    case MODE_DRAG:
    default:
        return "Drag";
    }
}

int ROI_FixRadius(roi_mode_t m)
{
    switch (m) {
    case MODE_FIX3:
        return 1;
    case MODE_FIX5:
        return 2;
    case MODE_DRAG:
    default:
        return 0;
    }
}

static void clamp_pt(POINT *p, int w, int h)
{
    if (p->x < 0)
        p->x = 0;
    if (p->y < 0)
        p->y = 0;
    if (p->x >= w)
        p->x = w - 1;
    if (p->y >= h)
        p->y = h - 1;
}

BOOL ROI_OnLDown(roi_state_t *s, const view_t *v, const image_t *img, POINT wp, RECT *out_img)
{
    POINT ip;
    int r;

    if (!s || !v || !img || !img->valid)
        return FALSE;
    if (!View_ToImage(v, img->w, img->h, wp, &ip))
        return FALSE; /* pressed outside the image: ignore */

    if (s->mode == MODE_DRAG) {
        /* ip already converted above; store in IMAGE coords so the rubber
           band follows window resizes (overlay reconverts per paint). */
        s->dragging = TRUE;
        s->anchor = ip;
        s->rubber.left = s->rubber.right = ip.x;
        s->rubber.top = s->rubber.bottom = ip.y;
        s->has_preview = FALSE;
        return FALSE; /* confirm on LUp */
    }

    /* Fixed modes: center clamped so the box always fits fully inside. */
    r = ROI_FixRadius(s->mode);
    if (ip.x < r)
        ip.x = r;
    if (ip.y < r)
        ip.y = r;
    if (ip.x > img->w - 1 - r)
        ip.x = img->w - 1 - r;
    if (ip.y > img->h - 1 - r)
        ip.y = img->h - 1 - r;
    s->preview = ip;
    s->has_preview = FALSE; /* clicked: becomes confirmed, not preview */
    s->confirmed.left = ip.x - r;
    s->confirmed.top = ip.y - r;
    s->confirmed.right = ip.x + r;
    s->confirmed.bottom = ip.y + r;
    s->has_confirmed = TRUE;
    if (out_img)
        *out_img = s->confirmed;
    return TRUE;
}

BOOL ROI_OnMove(roi_state_t *s, const view_t *v, const image_t *img, POINT wp)
{
    POINT ip;
    int r;

    if (!s || !v || !img || !img->valid)
        return FALSE;

    if (s->mode == MODE_DRAG) {
        if (!s->dragging)
            return FALSE;
        /* Convert to image coords so the rubber band survives resizes;
           clamp at the edges (mirror ROI_OnLUp) for live feedback. */
        if (!View_ToImage(v, img->w, img->h, wp, &ip)) {
            ip.x = wp.x - v->off_x;
            ip.y = wp.y - v->off_y;
            ip.x = v->scale > 0.0f ? (int)((float)ip.x / v->scale) : 0;
            ip.y = v->scale > 0.0f ? (int)((float)ip.y / v->scale) : 0;
            clamp_pt(&ip, img->w, img->h);
        }
        if (ip.x == s->rubber.right && ip.y == s->rubber.bottom)
            return FALSE; /* same cell: no repaint, no flicker */
        s->rubber.right = ip.x;
        s->rubber.bottom = ip.y;
        return TRUE;
    }

    if (!View_ToImage(v, img->w, img->h, wp, &ip))
        return FALSE;
    r = ROI_FixRadius(s->mode);
    if (ip.x < r)
        ip.x = r;
    if (ip.y < r)
        ip.y = r;
    if (ip.x > img->w - 1 - r)
        ip.x = img->w - 1 - r;
    if (ip.y > img->h - 1 - r)
        ip.y = img->h - 1 - r;
    if (s->has_preview && ip.x == s->preview.x && ip.y == s->preview.y)
        return FALSE; /* same cell: no repaint, no flicker */
    s->preview = ip;
    s->has_preview = TRUE;
    return TRUE;
}

BOOL ROI_OnLUp(roi_state_t *s, const view_t *v, const image_t *img, POINT wp, RECT *out_img)
{
    POINT a, b;

    if (!s || !v || !img || !img->valid)
        return FALSE;
    if (s->mode != MODE_DRAG || !s->dragging)
        return FALSE;
    s->dragging = FALSE;
    (void)v;
    (void)wp;

    /* Anchor/rubber are stored in IMAGE coords: normalize + clamp. */
    a = s->anchor;
    b.x = s->rubber.right;
    b.y = s->rubber.bottom;
    clamp_pt(&a, img->w, img->h);
    clamp_pt(&b, img->w, img->h);

    s->confirmed.left = (a.x < b.x) ? a.x : b.x;
    s->confirmed.top = (a.y < b.y) ? a.y : b.y;
    s->confirmed.right = (a.x > b.x) ? a.x : b.x;
    s->confirmed.bottom = (a.y > b.y) ? a.y : b.y;
    s->has_confirmed = TRUE;
    if (out_img)
        *out_img = s->confirmed;
    return TRUE; /* minimum box is 1x1 (a == b) */
}

/* Image-coord inclusive rect -> window rect (exclusive, for drawing). */
static void img_rect_to_window(const view_t *v, RECT img_rc, RECT *win_rc)
{
    POINT p0, p1;

    p0.x = img_rc.left;
    p0.y = img_rc.top;
    p1.x = img_rc.right + 1; /* exclusive edge */
    p1.y = img_rc.bottom + 1;
    View_ToWindow(v, p0, &p0);
    View_ToWindow(v, p1, &p1);
    win_rc->left = p0.x;
    win_rc->top = p0.y;
    win_rc->right = p1.x;
    win_rc->bottom = p1.y;
}

static void draw_rect_outline(HDC hdc, const RECT *rc, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ old_pen;
    HGDIOBJ old_brush;

    if (!pen)
        return;
    old_pen = SelectObject(hdc, pen);
    old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

void ROI_DrawOverlay(HDC hdc, const view_t *v, const roi_state_t *s)
{
    RECT wr;
    int r;

    if (!hdc || !v || !s)
        return;

    if (s->has_confirmed) {
        img_rect_to_window(v, s->confirmed, &wr);
        draw_rect_outline(hdc, &wr, RGB(255, 255, 0)); /* confirmed: yellow */
    }

    if (s->mode != MODE_DRAG && s->has_preview && !s->dragging) {
        RECT prv;
        r = ROI_FixRadius(s->mode);
        prv.left = s->preview.x - r;
        prv.top = s->preview.y - r;
        prv.right = s->preview.x + r;
        prv.bottom = s->preview.y + r;
        img_rect_to_window(v, prv, &wr);
        draw_rect_outline(hdc, &wr, RGB(255, 255, 255)); /* preview: white */
    }

    if (s->dragging) {
        /* Rubber band is IMAGE coords: reconvert via the current view so
           it tracks window resizes like the confirmed box. */
        RECT img_rc;
        img_rc.left = (s->anchor.x < s->rubber.right) ? s->anchor.x : s->rubber.right;
        img_rc.top = (s->anchor.y < s->rubber.bottom) ? s->anchor.y : s->rubber.bottom;
        img_rc.right = (s->anchor.x > s->rubber.right) ? s->anchor.x : s->rubber.right;
        img_rc.bottom = (s->anchor.y > s->rubber.bottom) ? s->anchor.y : s->rubber.bottom;
        img_rect_to_window(v, img_rc, &wr);
        draw_rect_outline(hdc, &wr, RGB(255, 255, 255)); /* rubber band: white */
    }
}
