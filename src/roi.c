#include "roi.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "view.h"

static BOOL point_to_image_clamped(const image_t *img, const view_t *view,
                                   POINT wp, POINT *ip)
{
    if (View_ToImage(view, img->w, img->h, wp, ip))
        return TRUE;
    if (!view || view->scale <= 0.0f || view->draw_w <= 0 || view->draw_h <= 0)
        return FALSE;
    ip->x = (int)((float)(wp.x - view->off_x) / view->scale);
    ip->y = (int)((float)(wp.y - view->off_y) / view->scale);
    if (ip->x < 0)
        ip->x = 0;
    if (ip->y < 0)
        ip->y = 0;
    if (ip->x >= img->w)
        ip->x = img->w - 1;
    if (ip->y >= img->h)
        ip->y = img->h - 1;
    return TRUE;
}

static RECT normalized_rect(POINT a, POINT b)
{
    RECT rc;
    rc.left = a.x < b.x ? a.x : b.x;
    rc.top = a.y < b.y ? a.y : b.y;
    rc.right = a.x > b.x ? a.x : b.x;
    rc.bottom = a.y > b.y ? a.y : b.y;
    return rc;
}

void ROI_Init(roi_list_t *list, drag_state_t *drag)
{
    if (list) {
        memset(list, 0, sizeof(*list));
        list->selected = -1;
    }
    if (drag)
        memset(drag, 0, sizeof(*drag));
}

void ROI_Clear(roi_list_t *list, drag_state_t *drag)
{
    if (list) {
        list->count = 0;
        list->selected = -1;
    }
    if (drag)
        memset(drag, 0, sizeof(*drag));
}

void ROI_ClearSource(roi_list_t *list, roi_source_t source)
{
    int i, out = 0;
    int selected_item;

    if (!list)
        return;
    selected_item = list->selected;
    for (i = 0; i < list->count; i++) {
        if (list->items[i].source != source) {
            if (out != i)
                list->items[out] = list->items[i];
            if (i == selected_item)
                list->selected = out;
            out++;
        } else if (i == selected_item) {
            list->selected = -1;
        }
    }
    list->count = out;
}

void ROI_Destroy(roi_list_t *list)
{
    if (!list)
        return;
    free(list->items);
    memset(list, 0, sizeof(*list));
    list->selected = -1;
}

BOOL ROI_Add(roi_list_t *list, const image_t *img, RECT rc, roi_source_t source)
{
    roi_item_t *items;
    int cap;

    if (!list || !img || !img->valid)
        return FALSE;
    if (list->count == list->cap) {
        if (list->cap > INT_MAX / 2)
            return FALSE;
        cap = list->cap ? list->cap * 2 : 8;
        items = (roi_item_t *)realloc(list->items, (size_t)cap * sizeof(*items));
        if (!items)
            return FALSE;
        list->items = items;
        list->cap = cap;
    }
    AnalyzeROI(img, rc, &list->items[list->count].res);
    if (list->items[list->count].res.count <= 0)
        return FALSE;
    rc.left = list->items[list->count].res.x0;
    rc.top = list->items[list->count].res.y0;
    rc.right = list->items[list->count].res.x1;
    rc.bottom = list->items[list->count].res.y1;
    list->items[list->count].rc = rc;
    list->items[list->count].source = source;
    list->selected = list->count;
    list->count++;
    return TRUE;
}

BOOL ROI_Remove(roi_list_t *list, int index)
{
    if (!list || index < 0 || index >= list->count)
        return FALSE;
    if (index + 1 < list->count)
        memmove(&list->items[index], &list->items[index + 1],
                (size_t)(list->count - index - 1) * sizeof(*list->items));
    list->count--;
    if (list->count == 0)
        list->selected = -1;
    else if (list->selected == index)
        list->selected = -1;
    else if (list->selected > index)
        list->selected--;
    return TRUE;
}

int ROI_SourceCount(const roi_list_t *list, roi_source_t source)
{
    int i, count = 0;
    if (!list)
        return 0;
    for (i = 0; i < list->count; i++)
        if (list->items[i].source == source)
            count++;
    return count;
}

int ROI_SourceIndex(const roi_list_t *list, int global_index)
{
    int i, index = 0;
    roi_source_t source;
    if (!list || global_index < 0 || global_index >= list->count)
        return -1;
    source = list->items[global_index].source;
    for (i = 0; i < global_index; i++)
        if (list->items[i].source == source)
            index++;
    return index;
}

roi_source_t ROI_ModeSource(roi_mode_t mode)
{
    switch (mode) {
    case MODE_GRID3:
        return ROI_SRC_GRID3;
    case MODE_GRID5:
        return ROI_SRC_GRID5;
    case MODE_DRAG:
    default:
        return ROI_SRC_MANUAL;
    }
}

BOOL ROI_BuildGrid(roi_list_t *list, const image_t *img, int n)
{
    int row, col;
    roi_source_t source;

    if (!list || !img || !img->valid || (n != 3 && n != 5))
        return FALSE;
    source = n == 3 ? ROI_SRC_GRID3 : ROI_SRC_GRID5;
    ROI_ClearSource(list, source);
    if (img->w < n || img->h < n)
        return FALSE;
    for (row = 0; row < n; row++) {
        int y0 = (int)(((long long)row * img->h) / n);
        int y1 = (int)(((long long)(row + 1) * img->h) / n) - 1;
        for (col = 0; col < n; col++) {
            RECT rc;
            rc.left = (int)(((long long)col * img->w) / n);
            rc.right = (int)(((long long)(col + 1) * img->w) / n) - 1;
            rc.top = y0;
            rc.bottom = y1;
            if (!ROI_Add(list, img, rc, source)) {
                ROI_ClearSource(list, source);
                return FALSE;
            }
        }
    }
    list->selected = -1;
    return TRUE;
}

int ROI_HitTest(const roi_list_t *list, POINT p)
{
    int i;

    if (!list)
        return -1;
    for (i = list->count - 1; i >= 0; i--) {
        const RECT *rc = &list->items[i].rc;
        if (p.x >= rc->left && p.x <= rc->right &&
            p.y >= rc->top && p.y <= rc->bottom)
            return i;
    }
    return -1;
}

BOOL ROI_OnLDown(roi_list_t *list, drag_state_t *drag, const image_t *img,
                 const view_t *view, POINT wp, BOOL additive)
{
    POINT ip;

    if (!list || !drag || !img || !img->valid ||
        !View_ToImage(view, img->w, img->h, wp, &ip))
        return FALSE;
    drag->dragging = TRUE;
    drag->additive = additive;
    drag->anchor_img = ip;
    drag->cur_img = ip;
    drag->down_win = wp;
    return TRUE;
}

BOOL ROI_OnMove(drag_state_t *drag, const image_t *img, const view_t *view,
                POINT wp)
{
    POINT ip;

    if (!drag || !drag->dragging || !img || !img->valid ||
        !point_to_image_clamped(img, view, wp, &ip))
        return FALSE;
    if (ip.x == drag->cur_img.x && ip.y == drag->cur_img.y)
        return FALSE;
    drag->cur_img = ip;
    return TRUE;
}

BOOL ROI_OnLUp(roi_list_t *list, drag_state_t *drag, const image_t *img,
               const view_t *view, POINT wp, BOOL *was_click)
{
    POINT end;
    long long dx, dy;
    BOOL click;

    if (!list || !drag || !drag->dragging || !img || !img->valid)
        return FALSE;
    if (!point_to_image_clamped(img, view, wp, &end))
        end = drag->cur_img;
    dx = wp.x - drag->down_win.x;
    dy = wp.y - drag->down_win.y;
    click = dx * dx + dy * dy < 9;
    drag->cur_img = end;
    drag->dragging = FALSE;
    if (was_click)
        *was_click = click;
    if (click) {
        list->selected = ROI_HitTest(list, end);
        return TRUE;
    }
    if (!drag->additive) {
        ROI_ClearSource(list, ROI_SRC_MANUAL);
    }
    return ROI_Add(list, img, normalized_rect(drag->anchor_img, end),
                   ROI_SRC_MANUAL);
}

const char *ROI_ModeName(roi_mode_t mode)
{
    switch (mode) {
    case MODE_GRID3:
        return "grid3x3";
    case MODE_GRID5:
        return "grid5x5";
    case MODE_DRAG:
    default:
        return "drag";
    }
}

const char *ROI_ModeLabel(roi_mode_t mode)
{
    switch (mode) {
    case MODE_GRID3:
        return "3x3";
    case MODE_GRID5:
        return "5x5";
    case MODE_DRAG:
    default:
        return "Drag";
    }
}
