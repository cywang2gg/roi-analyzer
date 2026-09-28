#ifndef ROI_METRICS_ASYNC_H
#define ROI_METRICS_ASYNC_H

#include <windows.h>

#include "compare.h"
#include "metrics.h"

#define METRICS_ASYNC_THRESHOLD_PX (4 * 1024 * 1024)
#define WM_APP_METRICS_DONE (WM_APP + 103)
#define IDD_METRICS_PROGRESS 240
#define IDC_METRICS_PBAR 241
#define IDC_METRICS_STATUS 242

typedef struct {
    int count;
    metrics_item_result_t items[CMP_MAX_CELLS];
    BOOL success;
    BOOL cancelled;
} metrics_async_result_t;

metrics_async_job_t *Metrics_StartAsync(HWND hwnd_notify, UINT done_message,
                                        cmp_image_t *const *images,
                                        const RECT *roi_rects, int count,
                                        BOOL enable_stage2);
const metrics_async_result_t *Metrics_AsyncResult(
    const metrics_async_job_t *job);
void Metrics_CancelAsync(metrics_async_job_t *job);
void Metrics_ReleaseAsync(metrics_async_job_t *job);
HWND Metrics_ShowProgress(HWND owner);
void Metrics_CloseProgress(HWND dialog);

#endif
