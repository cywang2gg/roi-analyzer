#ifndef ROI_TABLE_H
#define ROI_TABLE_H

#include <windows.h>

#include "roi.h"

HWND Table_Create(HWND parent, HINSTANCE instance, int control_id);
BOOL Table_Rebuild(HWND hwnd, const roi_list_t *rois, roi_source_t page);
BOOL Table_RebuildState(HWND hwnd, const roi_list_t *rois, roi_source_t page,
                        BOOL analysis_pending);
BOOL Table_AppendRow(HWND hwnd, int row, int global_index, int source_index,
                     const roi_item_t *item);
void Table_Select(HWND hwnd, int index);
int Table_FindRow(HWND hwnd, int global_index);
int Table_SelectedGlobalIndex(HWND hwnd);
void Table_Clear(HWND hwnd);

#endif
