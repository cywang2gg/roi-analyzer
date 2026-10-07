#include "table.h"

#include <commctrl.h>
#include <stdio.h>

#include "ui_scale.h"

static const char *const g_headers[] = {
    "#", "Rect", "Count", "R mean", "R std", "G mean", "G std",
    "B mean", "B std", "Y mean", "Y std", "L", "a", "b"
};

static const int g_widths[] = {
    38, 150, 65, 75, 70, 75, 70, 75, 70, 75, 70, 70, 70, 70
};

static void report_create_failure(const char *control)
{
    char message[160];
    DWORD error = GetLastError();
    snprintf(message, sizeof(message), "Create %s failed (GetLastError=%lu).",
             control, (unsigned long)error);
    MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
}

HWND Table_Create(HWND parent, HINSTANCE instance, int control_id)
{
    HWND hwnd;
    LVCOLUMNA col;
    int i;

    hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                           WS_CHILD | WS_VISIBLE | LVS_REPORT |
                           LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)control_id,
                           instance, NULL);
    if (!hwnd) {
        report_create_failure("ListView");
        return NULL;
    }
    ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT |
                                      LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    for (i = 0; i < (int)(sizeof(g_headers) / sizeof(g_headers[0])); i++) {
        col.pszText = (LPSTR)g_headers[i];
        col.cx = Ui_Scale(g_widths[i]);
        if (ListView_InsertColumn(hwnd, i, &col) == -1) {
            DWORD error = GetLastError();
            char message[160];
            snprintf(message, sizeof(message),
                     "Could not initialize ListView columns (GetLastError=%lu).",
                     (unsigned long)error);
            MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
            return NULL;
        }
    }
    return hwnd;
}

static BOOL append_row(HWND hwnd, int row_index, int global_index,
                       int source_index, const roi_item_t *item,
                       BOOL analysis_pending)
{
    char values[14][64];
    LVITEMA row;
    const roi_result_t *r;
    int i;

    if (!hwnd || !item || row_index < 0 || global_index < 0 || source_index < 0)
        return FALSE;
    r = &item->res;
    _snprintf(values[0], sizeof(values[0]), "%d", source_index + 1);
    _snprintf(values[1], sizeof(values[1]), "(%d,%d)-(%d,%d)",
              (int)item->rc.left, (int)item->rc.top,
              (int)item->rc.right, (int)item->rc.bottom);
    if (analysis_pending) {
        for (i = 2; i < 14; i++)
            strcpy(values[i], "-");
    } else {
        _snprintf(values[2], sizeof(values[2]), "%d", r->count);
        _snprintf(values[3], sizeof(values[3]), "%.2f", r->r_mean);
        _snprintf(values[4], sizeof(values[4]), "%.2f", r->r_std);
        _snprintf(values[5], sizeof(values[5]), "%.2f", r->g_mean);
        _snprintf(values[6], sizeof(values[6]), "%.2f", r->g_std);
        _snprintf(values[7], sizeof(values[7]), "%.2f", r->b_mean);
        _snprintf(values[8], sizeof(values[8]), "%.2f", r->b_std);
        _snprintf(values[9], sizeof(values[9]), "%.2f", r->y_mean);
        _snprintf(values[10], sizeof(values[10]), "%.2f", r->y_std);
        _snprintf(values[11], sizeof(values[11]), "%.2f", r->lab_l);
        _snprintf(values[12], sizeof(values[12]), "%.2f", r->lab_a);
        _snprintf(values[13], sizeof(values[13]), "%.2f", r->lab_b);
    }
    for (i = 0; i < 14; i++)
        values[i][sizeof(values[i]) - 1] = '\0';
    ZeroMemory(&row, sizeof(row));
    row.mask = LVIF_TEXT | LVIF_PARAM;
    row.iItem = row_index;
    row.lParam = (LPARAM)global_index;
    row.pszText = values[0];
    if (ListView_InsertItem(hwnd, &row) == -1)
        return FALSE;
    for (i = 1; i < 14; i++)
        ListView_SetItemText(hwnd, row_index, i, values[i]);
    return TRUE;
}

BOOL Table_AppendRow(HWND hwnd, int row_index, int global_index, int source_index,
                     const roi_item_t *item)
{
    return append_row(hwnd, row_index, global_index, source_index, item, FALSE);
}

void Table_Clear(HWND hwnd)
{
    if (hwnd)
        ListView_DeleteAllItems(hwnd);
}

BOOL Table_Rebuild(HWND hwnd, const roi_list_t *rois, roi_source_t page)
{
    return Table_RebuildState(hwnd, rois, page, FALSE);
}

BOOL Table_RebuildState(HWND hwnd, const roi_list_t *rois, roi_source_t page,
                        BOOL analysis_pending)
{
    int i, row = 0, selected_row = -1, top_index, top_global = -1;
    BOOL ok = TRUE;
    LVITEMA top_item;

    if (!hwnd || !rois)
        return FALSE;
    top_index = ListView_GetTopIndex(hwnd);
    if (top_index >= 0) {
        ZeroMemory(&top_item, sizeof(top_item));
        top_item.mask = LVIF_PARAM;
        top_item.iItem = top_index;
        if (ListView_GetItem(hwnd, &top_item))
            top_global = (int)top_item.lParam;
    }
    SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
    Table_Clear(hwnd);
    for (i = 0; i < rois->count; i++) {
        if (rois->items[i].source != page)
            continue;
        if (i == rois->selected)
            selected_row = row;
        if (!append_row(hwnd, row, i, ROI_SourceIndex(rois, i),
                        &rois->items[i], analysis_pending)) {
            ok = FALSE;
            break;
        }
        row++;
    }
    SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, NULL, TRUE);
    if (selected_row >= 0)
        ListView_SetItemState(hwnd, selected_row,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    if (top_global >= 0) {
        int restored_top = Table_FindRow(hwnd, top_global);
        if (restored_top >= 0)
            ListView_EnsureVisible(hwnd, restored_top, FALSE);
    }
    return ok;
}

int Table_FindRow(HWND hwnd, int global_index)
{
    int i, count;
    LVITEMA item;
    if (!hwnd || global_index < 0)
        return -1;
    count = ListView_GetItemCount(hwnd);
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    for (i = 0; i < count; i++) {
        item.iItem = i;
        if (ListView_GetItem(hwnd, &item) &&
            item.lParam == (LPARAM)global_index)
            return i;
    }
    return -1;
}

int Table_SelectedGlobalIndex(HWND hwnd)
{
    int row;
    LVITEMA item;
    if (!hwnd)
        return -1;
    row = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
    if (row < 0)
        return -1;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = row;
    return ListView_GetItem(hwnd, &item) ? (int)item.lParam : -1;
}

void Table_Select(HWND hwnd, int index)
{
    int count, i;
    if (!hwnd)
        return;
    count = ListView_GetItemCount(hwnd);
    for (i = 0; i < count; i++)
        ListView_SetItemState(hwnd, i, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (index >= 0 && index < count) {
        ListView_SetItemState(hwnd, index, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(hwnd, index, FALSE);
    }
}
