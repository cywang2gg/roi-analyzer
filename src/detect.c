#include "detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_messages.h"
#include "cc_cv.h"
#include "detect_worker.h"
#include "yolo_post.h"

static detect_state_t g_state = DETECT_DISABLED;
static volatile LONG g_image_seq;
static LONG g_pending_seq;
static yolo_detection_t g_results[DETECT_MAX_RESULTS];
static size_t g_result_count;
static double g_elapsed_ms;
static char g_error[128];
static int g_locate_state;
static char g_locate_error[64];

enum {
    LOCATE_IDLE,
    LOCATE_RUNNING,
    LOCATE_DONE,
    LOCATE_FAILED
};

static LONG current_seq(void)
{
    return InterlockedCompareExchange(&g_image_seq, 0, 0);
}

LONG Detect_ImageSeq(void)
{
    return current_seq();
}

static void clear_results(void)
{
    memset(g_results, 0, sizeof(g_results));
    g_result_count = 0;
    g_elapsed_ms = 0.0;
    g_error[0] = '\0';
}

static void clear_locate_result(void)
{
    memset(&g_app.locate_result, 0, sizeof(g_app.locate_result));
    g_app.locate_result_valid = FALSE;
    g_locate_state = LOCATE_IDLE;
    g_locate_error[0] = '\0';
}

static void submit_locate(const yolo_detection_t *detection, LONG seq)
{
    cc_image_view_t source;
    cc_image_t crop;
    locate_job_t *job;
    cc_box_t box;
    int origin_x;
    int origin_y;
    float rx;
    float ry;

    if (!g_app.locate_enabled || detection == NULL ||
        !g_app.img.valid || seq != current_seq()) {
        return;
    }
    source.pixels = g_app.img.px;
    source.width = g_app.img.w;
    source.height = g_app.img.h;
    source.stride = g_app.img.pitch;
    source.channels = 4;
    box.xc = (detection->x1 + detection->x2) * 0.5f;
    box.yc = (detection->y1 + detection->y2) * 0.5f;
    box.width = detection->x2 - detection->x1;
    box.height = detection->y2 - detection->y1;
    memset(&crop, 0, sizeof(crop));
    if (cc_crop_scale(source, box, 1.2f, 400, &crop, &origin_x, &origin_y,
                      &rx, &ry) != 0) {
        g_locate_state = LOCATE_FAILED;
        (void)snprintf(g_locate_error, sizeof(g_locate_error), "%s",
                       "crop_failed");
        return;
    }
    job = (locate_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        cc_image_free(&crop);
        g_locate_state = LOCATE_FAILED;
        (void)snprintf(g_locate_error, sizeof(g_locate_error), "%s",
                       "out_of_memory");
        return;
    }
    job->seq = seq;
    job->image = crop;
    job->origin_x = origin_x;
    job->origin_y = origin_y;
    job->rx = rx;
    job->ry = ry;
    if (!DetectWorker_SubmitLocate(job)) {
        cc_image_free(&job->image);
        free(job);
        g_locate_state = LOCATE_FAILED;
        (void)snprintf(g_locate_error, sizeof(g_locate_error), "%s",
                       "queue_failed");
        return;
    }
    g_locate_state = LOCATE_RUNNING;
    g_locate_error[0] = '\0';
}

BOOL Detect_Init(HWND hwnd_notify)
{
    char error[128] = "";

    g_state = DETECT_IDLE;
    g_pending_seq = current_seq();
    clear_results();
    clear_locate_result();
    if (!DetectWorker_Start(hwnd_notify, error, sizeof(error))) {
        g_state = DETECT_DISABLED;
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       error[0] != '\0' ? error :
                       "Could not start detection worker");
        g_error[sizeof(g_error) - 1] = '\0';
        return FALSE;
    }
    /* OpenCV is LoadLibrary-loaded (see D2): missing DLL only disables
       locate, the main program keeps running. */
    if (!cc_cv_load()) {
        OutputDebugStringA("ROI Analyzer: OpenCV unavailable, patch locate disabled.\n");
    }
    return TRUE;
}

void Detect_OnImageUnloading(void)
{
    if (g_app.hwnd_canvas != NULL)
        KillTimer(g_app.hwnd_canvas, IDT_DETECT_DELAY);
    (void)InterlockedIncrement(&g_image_seq);
    DetectWorker_CancelAll();
    clear_results();
    clear_locate_result();
    if (g_app.hwnd_canvas != NULL)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
    if (g_state != DETECT_DISABLED)
        g_state = DETECT_IDLE;
}

void Detect_OnImageLoaded(void)
{
    if (g_state == DETECT_DISABLED || g_app.hwnd_canvas == NULL ||
        !g_app.img.valid)
        return;
    g_pending_seq = current_seq();
    if (SetTimer(g_app.hwnd_canvas, IDT_DETECT_DELAY, 1000, NULL) == 0) {
        g_state = DETECT_FAILED;
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       "Could not start detection timer");
        g_error[sizeof(g_error) - 1] = '\0';
        return;
    }
    g_state = DETECT_PENDING;
}

void Detect_OnTimer(const unsigned char *bgra, int width, int height,
                    int stride)
{
    detect_job_t *job;
    LONG seq;
    const char *error;

    if (g_app.hwnd_canvas != NULL)
        KillTimer(g_app.hwnd_canvas, IDT_DETECT_DELAY);
    seq = current_seq();
    if (g_state != DETECT_PENDING || g_pending_seq != seq)
        return;
    if (bgra == NULL || width <= 0 || height <= 0 || stride < width * 4) {
        g_state = DETECT_FAILED;
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       "Invalid image buffer");
        g_error[sizeof(g_error) - 1] = '\0';
        return;
    }
    job = (detect_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        g_state = DETECT_FAILED;
        (void)snprintf(g_error, sizeof(g_error), "%s", "Out of memory");
        g_error[sizeof(g_error) - 1] = '\0';
        return;
    }
    job->rgb = (unsigned char *)malloc(
        (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U);
    if (job->rgb == NULL ||
        yolo_letterbox(bgra, width, height, stride, FALSE, job->rgb,
                       &job->transform) != 0) {
        error = job->rgb == NULL ? "Out of memory" :
                "Could not prepare image";
        free(job->rgb);
        free(job);
        g_state = DETECT_FAILED;
        (void)snprintf(g_error, sizeof(g_error), "%s", error);
        g_error[sizeof(g_error) - 1] = '\0';
        return;
    }
    job->seq = seq;
    job->image_width = width;
    job->image_height = height;
    if (!DetectWorker_Submit(job)) {
        free(job->rgb);
        free(job);
        g_state = DETECT_FAILED;
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       "Could not queue detection job");
        g_error[sizeof(g_error) - 1] = '\0';
        return;
    }
    g_state = DETECT_RUNNING;
}

void Detect_OnInitResult(detect_init_result_t *result)
{
    if (result == NULL || !result->success) {
        if (g_app.hwnd_canvas != NULL)
            KillTimer(g_app.hwnd_canvas, IDT_DETECT_DELAY);
        DetectWorker_CancelAll();
        clear_results();
        g_state = DETECT_DISABLED;
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       result != NULL && result->error[0] != '\0' ?
                       result->error : "Model initialization failed");
        g_error[sizeof(g_error) - 1] = '\0';
    }
    free(result);
}

void Detect_OnResult(detect_result_t *result)
{
    LONG seq;

    if (result == NULL)
        return;
    seq = current_seq();
    if (result->seq != seq || g_state != DETECT_RUNNING) {
        free(result);
        return;
    }
    clear_results();
    if (result->status == 0) {
        g_result_count = result->count > DETECT_MAX_RESULTS ?
            DETECT_MAX_RESULTS : result->count;
        memcpy(g_results, result->detections,
               g_result_count * sizeof(*g_results));
        g_elapsed_ms = result->elapsed_ms;
        g_state = DETECT_DONE;
    } else {
        (void)snprintf(g_error, sizeof(g_error), "%s",
                       result->error[0] != '\0' ? result->error :
                       "Inference failed");
        g_error[sizeof(g_error) - 1] = '\0';
        g_elapsed_ms = result->elapsed_ms;
        g_state = DETECT_FAILED;
    }
    if (result->status == 0 && g_result_count > 0U)
        submit_locate(&g_results[0], seq);
    free(result);
    if (g_app.hwnd_canvas != NULL)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
}

void Detect_OnLocateResult(cc_locate_result_t *result)
{
    if (result == NULL)
        return;
    if ((LONG)result->seq != current_seq()) {
        free(result);
        return;
    }
    g_app.locate_result = *result;
    g_app.locate_result_valid = TRUE;
    if (result->status == 0 && result->success) {
        g_locate_state = LOCATE_DONE;
        g_locate_error[0] = '\0';
    } else {
        g_locate_state = LOCATE_FAILED;
        (void)snprintf(g_locate_error, sizeof(g_locate_error), "%s",
                       result->reason[0] != '\0' ? result->reason :
                       "locate_failed");
        g_locate_error[sizeof(g_locate_error) - 1] = '\0';
    }
    free(result);
    if (g_app.hwnd_canvas != NULL)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
}

void Detect_OnLocateAllocationFailure(LONG seq)
{
    if (seq != current_seq())
        return;
    g_locate_state = LOCATE_FAILED;
    (void)snprintf(g_locate_error, sizeof(g_locate_error), "%s",
                   "out_of_memory");
    g_locate_error[sizeof(g_locate_error) - 1] = '\0';
    if (g_app.hwnd_canvas != NULL)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
}

void Detect_OnResultAllocationFailure(LONG seq)
{
    if (seq != current_seq() || g_state != DETECT_RUNNING)
        return;
    g_state = DETECT_FAILED;
    (void)snprintf(g_error, sizeof(g_error), "%s", "Out of memory");
    g_error[sizeof(g_error) - 1] = '\0';
    if (g_app.hwnd_canvas != NULL)
        InvalidateRect(g_app.hwnd_canvas, NULL, FALSE);
}

void Detect_GetStatusText(char *text, size_t capacity)
{
    if (text == NULL || capacity == 0)
        return;
    text[0] = '\0';
    switch (g_state) {
    case DETECT_DISABLED:
        if (g_error[0] != '\0')
            (void)snprintf(text, capacity, "Color chart detection: disabled (%s)",
                           g_error);
        break;
    case DETECT_RUNNING:
        (void)snprintf(text, capacity, "Detecting color charts...");
        break;
    case DETECT_DONE:
        if (g_result_count == 0)
            (void)snprintf(text, capacity, "No color charts detected (%.0f ms)",
                           g_elapsed_ms);
        else
            (void)snprintf(text, capacity, "Detected %u color chart%s (%.0f ms)",
                           (unsigned)g_result_count,
                           g_result_count == 1 ? "" : "s", g_elapsed_ms);
        break;
    case DETECT_FAILED:
        (void)snprintf(text, capacity, "Color chart detection failed: %s",
                       g_error);
        break;
    case DETECT_IDLE:
    case DETECT_PENDING:
        break;
    }
    text[capacity - 1] = '\0';
}

void Detect_GetLocateStatusText(char *text, size_t capacity)
{
    if (text == NULL || capacity == 0)
        return;
    text[0] = '\0';
    switch (g_locate_state) {
    case LOCATE_RUNNING:
        (void)snprintf(text, capacity, "Locating patches...");
        break;
    case LOCATE_DONE:
        (void)snprintf(text, capacity, "Located %u patches (%.0f ms)",
                       (unsigned)g_app.locate_result.valid_count,
                       g_app.locate_result.elapsed_ms);
        break;
    case LOCATE_FAILED:
        (void)snprintf(text, capacity, "Locate failed: %s",
                       g_locate_error[0] != '\0' ?
                       g_locate_error : "unknown_error");
        break;
    case LOCATE_IDLE:
        break;
    }
    text[capacity - 1] = '\0';
}

size_t Detect_GetResults(const yolo_detection_t **detections)
{
    if (detections != NULL)
        *detections = g_results;
    return g_result_count;
}

void Detect_Shutdown(void)
{
    MSG message;

    Detect_OnImageUnloading();
    DetectWorker_Stop();
    cc_cv_unload();
    while (g_app.hwnd_main != NULL &&
           PeekMessageA(&message, g_app.hwnd_main, WM_APP_DETECT_DONE,
                        WM_APP_DETECT_DONE, PM_REMOVE)) {
        if (message.wParam != DETECT_RESULT_ALLOCATION_FAILURE)
            free((void *)message.lParam);
    }
    while (g_app.hwnd_main != NULL &&
           PeekMessageA(&message, g_app.hwnd_main, WM_APP_DETECT_INIT,
                        WM_APP_DETECT_INIT, PM_REMOVE))
        free((void *)message.lParam);
    while (g_app.hwnd_main != NULL &&
           PeekMessageA(&message, g_app.hwnd_main, WM_APP_LOCATE_DONE,
                        WM_APP_LOCATE_DONE, PM_REMOVE)) {
        if (message.wParam != LOCATE_RESULT_ALLOCATION_FAILURE)
            free((void *)message.lParam);
    }
}
