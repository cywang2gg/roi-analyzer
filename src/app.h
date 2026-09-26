#ifndef ROI_APP_H
#define ROI_APP_H

#include <windows.h>

#include "image.h"
#include "roi.h"
#include "view.h"

typedef struct {
    HWND hwnd_main, hwnd_canvas, hwnd_table, hwnd_tabs, hwnd_status;
    HWND hwnd_btn_export, hwnd_btn_clear, hwnd_chk_multi;
    image_t img;
    view_t view;
    roi_list_t rois;
    drag_state_t drag;
    roi_mode_t mode;
    roi_mode_t table_page;
    BOOL multi;
} app_t;

extern app_t g_app;

void App_RoiChanged(void);
void App_SetTablePage(roi_mode_t page);
void App_SelectROI(int global_index);
void App_UpdateStatus(void);

#endif
