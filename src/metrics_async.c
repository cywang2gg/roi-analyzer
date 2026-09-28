#include "metrics_async.h"

#include <objbase.h>
#include <stdlib.h>
#include <string.h>

#include <commctrl.h>

#include "ranking.h"

struct metrics_async_job {
    volatile LONG refs;
    HWND hwnd_notify;
    UINT done_message;
    int count;
    cmp_image_t *images[CMP_MAX_CELLS];
    RECT roi_rects[CMP_MAX_CELLS];
    char names[CMP_MAX_CELLS][MAX_PATH];
    BOOL enable_stage2;
    volatile BOOL cancel_requested;
    HANDLE thread_handle;
    metrics_async_result_t result;
};

static void job_unref(metrics_async_job_t *job)
{
    if (InterlockedDecrement(&job->refs) == 0) {
        int i;
        if (job->thread_handle)
            CloseHandle(job->thread_handle);
        for (i = 0; i < job->result.count; i++)
            Metrics_FreeItemResult(&job->result.items[i]);
        free(job);
    }
}

static DWORD WINAPI metrics_worker(void *parameter)
{
    metrics_async_job_t *job = (metrics_async_job_t *)parameter;
    metrics_masks_t reference_masks;
    HRESULT com_result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL uninitialize_com = SUCCEEDED(com_result);
    BOOL have_reference_masks = FALSE;
    int i;
    memset(&reference_masks, 0, sizeof(reference_masks));
    job->result.success = SUCCEEDED(com_result);
    if (job->result.success) {
        have_reference_masks = MetricsMasks_BuildCancelable(
            &job->images[0]->img, job->roi_rects[0],
            &job->cancel_requested, &reference_masks);
        if (!have_reference_masks)
            job->result.success = FALSE;
    }
    for (i = 0; job->result.success && i < job->count; i++) {
        metrics_item_result_t *item = &job->result.items[i];
        if (InterlockedCompareExchange(
                (volatile LONG *)&job->cancel_requested, 0, 0)) {
            job->result.success = FALSE;
            job->result.cancelled = TRUE;
            break;
        }
        if (!Metrics_AnalyzeROI_CancelableWithReferenceMask(
                &job->images[i]->img, job->roi_rects[i],
                job->enable_stage2, &job->cancel_requested,
                &reference_masks, item)) {
            job->result.success = FALSE;
            job->result.cancelled =
                InterlockedCompareExchange(
                    (volatile LONG *)&job->cancel_requested, 0, 0) != 0;
            break;
        }
        lstrcpynA(item->image_name, job->names[i],
                   (int)sizeof(item->image_name));
        job->result.count++;
    }
    if (job->result.success) {
        Metrics_ApplyReference(job->result.items, job->result.count, 0);
        MetricsRank_Compute(job->result.items, job->result.count,
                            METRICS_PROFILE_BALANCED,
                            METRICS_RANK_ABSOLUTE_DEFAULT);
    }
    if (have_reference_masks)
        MetricsMasks_Free(&reference_masks);
    if (InterlockedCompareExchange(
            (volatile LONG *)&job->cancel_requested, 0, 0)) {
        job->result.success = FALSE;
        job->result.cancelled = TRUE;
    }
    for (i = 0; i < job->count; i++) {
        CmpImage_Unref(job->images[i]);
        job->images[i] = NULL;
    }
    if (uninitialize_com)
        CoUninitialize();
    MemoryBarrier();
    if (!PostMessageA(job->hwnd_notify, job->done_message,
                      (WPARAM)job, 0))
        OutputDebugStringA("ROI Analyzer: could not post metrics completion.\n");
    job_unref(job);
    return 0;
}

static INT_PTR CALLBACK progress_dialog_proc(HWND dialog, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    (void)lparam;
    if (message == WM_INITDIALOG) {
        HWND progress = GetDlgItem(dialog, IDC_METRICS_PBAR);
        SetWindowTextA(GetDlgItem(dialog, IDC_METRICS_STATUS),
                       "Analyzing visible image regions...");
        if (progress)
            SendMessageA(progress, PBM_SETMARQUEE, TRUE, 30);
        return TRUE;
    }
    if (message == WM_CLOSE) {
        ShowWindow(dialog, SW_HIDE);
        return TRUE;
    }
    return FALSE;
}

metrics_async_job_t *Metrics_StartAsync(HWND hwnd_notify, UINT done_message,
                                        cmp_image_t *const *images,
                                        const RECT *roi_rects, int count,
                                        BOOL enable_stage2)
{
    metrics_async_job_t *job;
    int i;
    if (!hwnd_notify || !done_message || !images || !roi_rects ||
        count <= 0 || count > CMP_MAX_CELLS)
        return NULL;
    job = (metrics_async_job_t *)calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->refs = 2;
    job->hwnd_notify = hwnd_notify;
    job->done_message = done_message;
    job->count = count;
    job->enable_stage2 = enable_stage2;
    for (i = 0; i < count; i++) {
        if (!images[i] || !images[i]->img.valid || !images[i]->img.px)
            break;
        job->images[i] = CmpImage_Ref(images[i]);
        job->roi_rects[i] = roi_rects[i];
        lstrcpynA(job->names[i], images[i]->name,
                  (int)sizeof(job->names[i]));
    }
    if (i != count) {
        while (i > 0)
            CmpImage_Unref(job->images[--i]);
        free(job);
        return NULL;
    }
    job->thread_handle = CreateThread(NULL, 0, metrics_worker, job, 0, NULL);
    if (!job->thread_handle) {
        for (i = 0; i < count; i++)
            CmpImage_Unref(job->images[i]);
        free(job);
        return NULL;
    }
    return job;
}

const metrics_async_result_t *Metrics_AsyncResult(
    const metrics_async_job_t *job)
{
    if (!job)
        return NULL;
    MemoryBarrier();
    return &job->result;
}

void Metrics_CancelAsync(metrics_async_job_t *job)
{
    if (!job)
        return;
    InterlockedExchange((volatile LONG *)&job->cancel_requested, TRUE);
    if (job->thread_handle)
        WaitForSingleObject(job->thread_handle, 1000);
}

void Metrics_ReleaseAsync(metrics_async_job_t *job)
{
    if (job)
        job_unref(job);
}

HWND Metrics_ShowProgress(HWND owner)
{
    HWND dialog = CreateDialogParamA(GetModuleHandleA(NULL),
                                     MAKEINTRESOURCEA(IDD_METRICS_PROGRESS),
                                     owner, progress_dialog_proc, 0);
    if (dialog) {
        RECT owner_rect, dialog_rect;
        if (GetWindowRect(owner, &owner_rect) &&
            GetWindowRect(dialog, &dialog_rect)) {
            int x = owner_rect.left +
                    ((owner_rect.right - owner_rect.left) -
                     (dialog_rect.right - dialog_rect.left)) / 2;
            int y = owner_rect.top +
                    ((owner_rect.bottom - owner_rect.top) -
                     (dialog_rect.bottom - dialog_rect.top)) / 2;
            SetWindowPos(dialog, NULL, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ShowWindow(dialog, SW_SHOWNORMAL);
    }
    return dialog;
}

void Metrics_CloseProgress(HWND dialog)
{
    if (dialog && IsWindow(dialog))
        DestroyWindow(dialog);
}
