#ifndef ROI_APP_H
#define ROI_APP_H

#include <windows.h>

#include "image.h"
#include "histpanel.h"
#include "filelist.h"
#include "roi.h"
#include "view.h"

#define IDM_COMPARE_FILES 155
#define IDM_COMPARE_NEXT  156

typedef struct {
    HWND hwnd_main, hwnd_canvas, hwnd_table, hwnd_tabs, hwnd_status, hwnd_hist;
    HWND hwnd_btn_export, hwnd_btn_clear, hwnd_chk_multi;
    image_t img;
    view_t view;
    view_pyr_t pyramid;
    filelist_t files;
    int file_idx;
    unsigned int pyramid_gen;
    BOOL pyramid_attempted;
    BOOL pyramid_pending;
    double paint_ms;
    BOOL paint_pending;
    roi_list_t rois;
    drag_state_t drag;
    roi_mode_t mode;
    roi_mode_t table_page;
    BOOL multi;
    BOOL show_hist;
    BOOL analysis_stale;
    BOOL is_modified;
    unsigned int img_gen;
} app_t;

extern app_t g_app;

void App_Navigate(int direction, BOOL light);
void App_FlushPending(void);
BOOL App_NavKeyAllowed(const MSG *msg);
double App_Ms(LARGE_INTEGER start);
void App_RoiChanged(void);
void App_SetTablePage(roi_mode_t page);
void App_SelectROI(int global_index);
void App_UpdateStatus(void);
void App_StatusLayout(void);
void App_StatusSetIndex(void);
void App_UpdateHistogram(void);
void App_PreviewHistogram(RECT img_rc);

#endif
