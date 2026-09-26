#ifndef ROI_ROI_H
#define ROI_ROI_H

#include <windows.h>

#include "analyze.h"

struct image_s;
typedef struct image_s image_t;
struct view_s;
typedef struct view_s view_t;

typedef enum { MODE_DRAG, MODE_GRID3, MODE_GRID5 } roi_mode_t;
typedef enum { ROI_SRC_MANUAL, ROI_SRC_GRID3, ROI_SRC_GRID5 } roi_source_t;

typedef struct {
    RECT rc;
    roi_source_t source;
    roi_result_t res;
} roi_item_t;

typedef struct {
    roi_item_t *items;
    int count;
    int cap;
    int selected;
} roi_list_t;

typedef struct {
    BOOL dragging;
    BOOL additive;
    POINT anchor_img;
    POINT cur_img;
    POINT down_win;
} drag_state_t;

void ROI_Init(roi_list_t *list, drag_state_t *drag);
void ROI_SetSelected(roi_list_t *list, int index);
void ROI_Clear(roi_list_t *list, drag_state_t *drag);
void ROI_ClearSource(roi_list_t *list, roi_source_t source);
void ROI_Destroy(roi_list_t *list);
BOOL ROI_Add(roi_list_t *list, const image_t *img, RECT rc, roi_source_t source);
BOOL ROI_Remove(roi_list_t *list, int index);
BOOL ROI_BuildGrid(roi_list_t *list, const image_t *img, int n);
int ROI_SourceCount(const roi_list_t *list, roi_source_t source);
int ROI_SourceIndex(const roi_list_t *list, int global_index);
roi_source_t ROI_ModeSource(roi_mode_t mode);
int ROI_HitTest(const roi_list_t *list, POINT image_point);
BOOL ROI_OnLDown(roi_list_t *list, drag_state_t *drag, const image_t *img,
                 const view_t *view, POINT window_point, BOOL additive);
BOOL ROI_OnMove(drag_state_t *drag, const image_t *img, const view_t *view,
                POINT window_point);
BOOL ROI_OnLUp(roi_list_t *list, drag_state_t *drag, const image_t *img,
               const view_t *view, POINT window_point, BOOL *was_click);
const char *ROI_ModeName(roi_mode_t mode);
const char *ROI_ModeLabel(roi_mode_t mode);

#endif
