#include "detect_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_messages.h"
#include "detect.h"
#include "yolo_ort.h"

#define DETECT_RGB_BYTES ((size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U)
#define DETECT_FLOAT_COUNT ((size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U)
static HANDLE g_worker_thread;
static HANDLE g_job_event;
static HANDLE g_quit_event;
static CRITICAL_SECTION g_worker_lock;
static BOOL g_worker_lock_initialized;
static BOOL g_worker_started;
static HWND g_hwnd_notify;
static detect_job_t *g_job_slot;
static locate_job_t *g_locate_job_slot;
static yolo_ort_session_t *g_session;
static OrtRunOptions *g_active_run_options;
static int64_t g_qpc_frequency;

static void copy_error(char *destination, size_t capacity,
                       const char *source)
{
    if (destination != NULL && capacity != 0) {
        (void)snprintf(destination, capacity, "%s",
                       source != NULL ? source : "Detection failed");
        destination[capacity - 1] = '\0';
    }
}

static void free_job(detect_job_t *job)
{
    if (job != NULL) {
        free(job->rgb);
        free(job);
    }
}

static void free_locate_job(locate_job_t *job)
{
    if (job != NULL) {
        cc_image_free(&job->image);
        free(job);
    }
}

static BOOL cancelled(LONG seq)
{
    return Detect_ImageSeq() != seq ||
           WaitForSingleObject(g_quit_event, 0) == WAIT_OBJECT_0;
}

static BOOL build_model_path(char *path, size_t capacity)
{
    DWORD length;
    char *slash;
    size_t prefix;

    if (path == NULL || capacity < MAX_PATH)
        return FALSE;
    length = GetModuleFileNameA(NULL, path, (DWORD)capacity);
    if (length == 0 || length >= capacity)
        return FALSE;
    slash = strrchr(path, '\\');
    if (slash == NULL)
        slash = strrchr(path, '/');
    if (slash == NULL)
        return FALSE;
    prefix = (size_t)(slash - path) + 1U;
    if (prefix + sizeof("models\\color_chart.onnx") > capacity)
        return FALSE;
    memcpy(path + prefix, "models\\color_chart.onnx",
           sizeof("models\\color_chart.onnx"));
    return TRUE;
}

static BOOL post_init_result(BOOL success, const char *error)
{
    detect_init_result_t *result;

    result = (detect_init_result_t *)calloc(1, sizeof(*result));
    if (result == NULL) {
        if (!PostMessageA(g_hwnd_notify, WM_APP_DETECT_INIT, 0, 0))
            OutputDebugStringA("ROI Analyzer: could not post detection initialization failure.\n");
        return FALSE;
    }
    result->success = success;
    if (!success)
        copy_error(result->error, sizeof(result->error), error);
    if (!PostMessageA(g_hwnd_notify, WM_APP_DETECT_INIT, 0, (LPARAM)result)) {
        OutputDebugStringA("ROI Analyzer: could not post detection init result.\n");
        free(result);
        return FALSE;
    }
    return TRUE;
}

static void post_result_allocation_failure(LONG seq)
{
    if (!PostMessageA(g_hwnd_notify, WM_APP_DETECT_DONE,
                      DETECT_RESULT_ALLOCATION_FAILURE, (LPARAM)seq))
        OutputDebugStringA("ROI Analyzer: could not post detection allocation failure.\n");
}

static int run_model(const float *input, LONG seq, BOOL cancellable,
                     const float **output, char *error, size_t error_capacity)
{
    const OrtApi *api = yolo_ort_get_api(g_session);
    OrtRunOptions *run_options = NULL;
    OrtStatus *status;
    int run_status = -1;
    BOOL registered = FALSE;
    BOOL may_run = TRUE;

    status = api->CreateRunOptions(&run_options);
    if (status != NULL) {
        copy_error(error, error_capacity, api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        return -1;
    }
    if (yolo_ort_register_run_options(g_session, run_options) != 0) {
        copy_error(error, error_capacity, "Could not register inference options");
        api->ReleaseRunOptions(run_options);
        return -1;
    }
    registered = TRUE;
    if (cancellable) {
        EnterCriticalSection(&g_worker_lock);
        if (cancelled(seq)) {
            may_run = FALSE;
        } else {
            g_active_run_options = run_options;
        }
        LeaveCriticalSection(&g_worker_lock);
    }
    if (may_run)
        run_status = yolo_ort_run(g_session, input, run_options, output,
                                  error, error_capacity);
    if (cancellable) {
        EnterCriticalSection(&g_worker_lock);
        if (g_active_run_options == run_options)
            g_active_run_options = NULL;
        LeaveCriticalSection(&g_worker_lock);
    }
    if (registered)
        yolo_ort_unregister_run_options(g_session, run_options);
    api->ReleaseRunOptions(run_options);
    if (!may_run)
        return 1;
    return run_status;
}

static void normalize_rgb(const unsigned char *rgb, float *nchw)
{
    size_t plane = (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE;
    size_t i;

    for (i = 0; i < plane; ++i) {
        nchw[i] = (float)rgb[i * 3U] / 255.0f;
        nchw[plane + i] = (float)rgb[i * 3U + 1U] / 255.0f;
        nchw[plane * 2U + i] = (float)rgb[i * 3U + 2U] / 255.0f;
    }
}

static detect_result_t *process_job(detect_job_t *job, float *input)
{
    detect_result_t *result;
    const float *output = NULL;
    LARGE_INTEGER started = { 0 }, finished = { 0 };
    size_t detection_count = 0;
    int run_status;
    char error[128] = "";

    if (cancelled(job->seq))
        return NULL;
    normalize_rgb(job->rgb, input);
    if (cancelled(job->seq))
        return NULL;
    QueryPerformanceCounter(&started);
    run_status = run_model(input, job->seq, TRUE, &output, error,
                           sizeof(error));
    if (cancelled(job->seq))
        return NULL;
    if (run_status != 0) {
        result = (detect_result_t *)calloc(1, sizeof(*result));
        if (result == NULL) {
            OutputDebugStringA("ROI Analyzer: could not allocate detection result.\n");
            return NULL;
        }
        result->seq = job->seq;
        result->status = -1;
        copy_error(result->error, sizeof(result->error),
                   error[0] != '\0' ? error : "Inference failed");
        QueryPerformanceCounter(&finished);
        result->elapsed_ms = g_qpc_frequency != 0 ?
            (double)(finished.QuadPart - started.QuadPart) * 1000.0 /
                (double)g_qpc_frequency : 0.0;
        return result;
    }
    if (cancelled(job->seq))
        return NULL;
    result = (detect_result_t *)calloc(1, sizeof(*result));
    if (result == NULL) {
        OutputDebugStringA("ROI Analyzer: could not allocate detection result.\n");
        return NULL;
    }
    result->seq = job->seq;
    if (yolo_decode_v8(output, &job->transform, YOLO_DEFAULT_CONFIDENCE,
                       YOLO_DEFAULT_IOU, DETECT_MAX_RESULTS,
                       result->detections, DETECT_MAX_RESULTS,
                       &detection_count) != 0) {
        result->status = -1;
        copy_error(result->error, sizeof(result->error),
                   "Could not decode model output");
    } else {
        result->count = detection_count;
    }
    if (cancelled(job->seq)) {
        free(result);
        return NULL;
    }
    QueryPerformanceCounter(&finished);
    result->elapsed_ms = g_qpc_frequency != 0 ?
        (double)(finished.QuadPart - started.QuadPart) * 1000.0 /
            (double)g_qpc_frequency : 0.0;
    return result;
}

static cc_locate_result_t *process_locate_job(locate_job_t *job)
{
    cc_locate_result_t *result;
    cc_image_view_t image;
    LARGE_INTEGER started = { 0 }, finished = { 0 };
    int run_status;

    if (cancelled(job->seq))
        return NULL;
    result = (cc_locate_result_t *)calloc(1, sizeof(*result));
    if (result == NULL)
        return NULL;
    image.pixels = job->image.pixels;
    image.width = job->image.width;
    image.height = job->image.height;
    image.stride = job->image.stride;
    image.channels = job->image.channels;
    QueryPerformanceCounter(&started);
    run_status = cc_locate_run(image, job->origin_x, job->origin_y,
                               job->rx, job->ry, result);
    if (cancelled(job->seq)) {
        free(result);
        return NULL;
    }
    result->seq = (int32_t)job->seq;
    result->status = run_status;
    QueryPerformanceCounter(&finished);
    result->elapsed_ms = g_qpc_frequency != 0 ?
        (double)(finished.QuadPart - started.QuadPart) * 1000.0 /
            (double)g_qpc_frequency : 0.0;
    return result;
}

static void post_locate_allocation_failure(LONG seq)
{
    if (!PostMessageA(g_hwnd_notify, WM_APP_LOCATE_DONE,
                      LOCATE_RESULT_ALLOCATION_FAILURE, (LPARAM)seq))
        OutputDebugStringA("ROI Analyzer: could not post locate allocation failure.\n");
}

static DWORD WINAPI detect_worker(void *parameter)
{
    char model_path[MAX_PATH];
    char error[128] = "";
    float *input = NULL;
    unsigned char *warmup_rgb = NULL;
    const float *warmup_output = NULL;
    LONG warmup_seq;
    LARGE_INTEGER frequency = { 0 };
    HANDLE wait_handles[2];
    DWORD wait_result;
    yolo_ort_session_t *session = NULL;

    (void)parameter;
    input = (float *)malloc(DETECT_FLOAT_COUNT * sizeof(*input));
    warmup_rgb = (unsigned char *)malloc(DETECT_RGB_BYTES);
    if (input == NULL || warmup_rgb == NULL ||
        !build_model_path(model_path, sizeof(model_path))) {
        copy_error(error, sizeof(error),
                   input == NULL || warmup_rgb == NULL ?
                   "Out of memory" : "Cannot locate the model file");
        post_init_result(FALSE, error);
        free(input);
        free(warmup_rgb);
        input = NULL;
        warmup_rgb = NULL;
        goto wait_for_shutdown;
    }
    if (yolo_ort_load(&session, model_path, error, sizeof(error)) != 0) {
        post_init_result(FALSE, error);
        goto wait_for_shutdown;
    }
    EnterCriticalSection(&g_worker_lock);
    g_session = session;
    LeaveCriticalSection(&g_worker_lock);
    QueryPerformanceFrequency(&frequency);
    g_qpc_frequency = frequency.QuadPart;
    memset(warmup_rgb, 114, DETECT_RGB_BYTES);
    normalize_rgb(warmup_rgb, input);
    warmup_seq = Detect_ImageSeq();
    if (run_model(input, warmup_seq, FALSE, &warmup_output,
                  error, sizeof(error)) != 0) {
        post_init_result(FALSE, error[0] != '\0' ? error :
                         "Model warm-up failed");
        yolo_ort_unload(session);
        EnterCriticalSection(&g_worker_lock);
        g_session = NULL;
        LeaveCriticalSection(&g_worker_lock);
        session = NULL;
        goto wait_for_shutdown;
    }
    if (!post_init_result(TRUE, NULL)) {
        yolo_ort_unload(session);
        EnterCriticalSection(&g_worker_lock);
        g_session = NULL;
        LeaveCriticalSection(&g_worker_lock);
        session = NULL;
        goto wait_for_shutdown;
    }
    wait_handles[0] = g_quit_event;
    wait_handles[1] = g_job_event;

    for (;;) {
        detect_job_t *job;
        locate_job_t *locate_job;
        detect_result_t *result;
        cc_locate_result_t *locate_result;
        LONG job_seq;

        wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED)
            break;
        EnterCriticalSection(&g_worker_lock);
        job = g_job_slot;
        g_job_slot = NULL;
        locate_job = g_locate_job_slot;
        g_locate_job_slot = NULL;
        LeaveCriticalSection(&g_worker_lock);
        if (job == NULL && locate_job == NULL)
            continue;
        if (job != NULL) {
            if (cancelled(job->seq)) {
                free_job(job);
            } else {
                job_seq = job->seq;
                result = process_job(job, input);
                free_job(job);
                if (result == NULL) {
                    if (!cancelled(job_seq))
                        post_result_allocation_failure(job_seq);
                } else if (cancelled(result->seq) ||
                           !PostMessageA(g_hwnd_notify, WM_APP_DETECT_DONE,
                                         0, (LPARAM)result)) {
                    if (!cancelled(result->seq))
                        OutputDebugStringA("ROI Analyzer: could not post detection result.\n");
                    free(result);
                }
            }
        }
        if (locate_job != NULL) {
            if (cancelled(locate_job->seq)) {
                free_locate_job(locate_job);
            } else {
                job_seq = locate_job->seq;
                locate_result = process_locate_job(locate_job);
                free_locate_job(locate_job);
                if (locate_result == NULL) {
                    if (!cancelled(job_seq))
                        post_locate_allocation_failure(job_seq);
                } else if (cancelled((LONG)locate_result->seq) ||
                           !PostMessageA(g_hwnd_notify, WM_APP_LOCATE_DONE,
                                         0, (LPARAM)locate_result)) {
                    if (!cancelled((LONG)locate_result->seq))
                        OutputDebugStringA("ROI Analyzer: could not post locate result.\n");
                    free(locate_result);
                }
            }
        }
    }
    free(input);
    input = NULL;
    free(warmup_rgb);
    warmup_rgb = NULL;
    yolo_ort_unload(session);
    EnterCriticalSection(&g_worker_lock);
    g_session = NULL;
    LeaveCriticalSection(&g_worker_lock);
    session = NULL;

wait_for_shutdown:
    if (input != NULL)
        free(input);
    if (warmup_rgb != NULL)
        free(warmup_rgb);
    if (session != NULL) {
        yolo_ort_unload(session);
        EnterCriticalSection(&g_worker_lock);
        g_session = NULL;
        LeaveCriticalSection(&g_worker_lock);
    }
    return 0;
}

BOOL DetectWorker_Start(HWND hwnd_notify, char *error, size_t error_capacity)
{
    if (hwnd_notify == NULL || g_worker_started) {
        copy_error(error, error_capacity, "Invalid detection worker state");
        return FALSE;
    }
    g_job_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_quit_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_job_event == NULL || g_quit_event == NULL) {
        copy_error(error, error_capacity, "Could not create worker events");
        if (g_job_event != NULL)
            CloseHandle(g_job_event);
        if (g_quit_event != NULL)
            CloseHandle(g_quit_event);
        g_job_event = NULL;
        g_quit_event = NULL;
        return FALSE;
    }
    InitializeCriticalSection(&g_worker_lock);
    g_worker_lock_initialized = TRUE;
    g_hwnd_notify = hwnd_notify;
    g_worker_thread = CreateThread(NULL, 0, detect_worker, NULL, 0, NULL);
    if (g_worker_thread == NULL) {
        copy_error(error, error_capacity, "Could not create detection thread");
        DeleteCriticalSection(&g_worker_lock);
        g_worker_lock_initialized = FALSE;
        CloseHandle(g_job_event);
        CloseHandle(g_quit_event);
        g_job_event = NULL;
        g_quit_event = NULL;
        return FALSE;
    }
    (void)SetThreadPriority(g_worker_thread, THREAD_PRIORITY_BELOW_NORMAL);
    g_worker_started = TRUE;
    return TRUE;
}

BOOL DetectWorker_Submit(detect_job_t *job)
{
    detect_job_t *old_job;

    if (!g_worker_started || job == NULL)
        return FALSE;
    EnterCriticalSection(&g_worker_lock);
    old_job = g_job_slot;
    g_job_slot = job;
    if (!SetEvent(g_job_event)) {
        g_job_slot = old_job;
        LeaveCriticalSection(&g_worker_lock);
        return FALSE;
    }
    LeaveCriticalSection(&g_worker_lock);
    free_job(old_job);
    return TRUE;
}

BOOL DetectWorker_SubmitLocate(locate_job_t *job)
{
    locate_job_t *old_job;

    if (!g_worker_started || job == NULL)
        return FALSE;
    EnterCriticalSection(&g_worker_lock);
    old_job = g_locate_job_slot;
    g_locate_job_slot = job;
    if (!SetEvent(g_job_event)) {
        g_locate_job_slot = old_job;
        LeaveCriticalSection(&g_worker_lock);
        return FALSE;
    }
    LeaveCriticalSection(&g_worker_lock);
    free_locate_job(old_job);
    return TRUE;
}

void DetectWorker_CancelAll(void)
{
    detect_job_t *job;
    locate_job_t *locate_job;
    char error[128];

    if (!g_worker_lock_initialized)
        return;
    EnterCriticalSection(&g_worker_lock);
    job = g_job_slot;
    g_job_slot = NULL;
    locate_job = g_locate_job_slot;
    g_locate_job_slot = NULL;
    if (g_session != NULL && g_active_run_options != NULL &&
        yolo_ort_terminate(g_session, error, sizeof(error)) != 0)
        OutputDebugStringA("ROI Analyzer: could not terminate inference run.\n");
    LeaveCriticalSection(&g_worker_lock);
    free_job(job);
    free_locate_job(locate_job);
}

void DetectWorker_Stop(void)
{
    DWORD wait_result;

    if (!g_worker_started)
        return;
    (void)SetEvent(g_quit_event);
    wait_result = WaitForSingleObject(g_worker_thread, 3000);
    if (wait_result != WAIT_OBJECT_0) {
        OutputDebugStringA("ROI Analyzer: detection worker did not stop in time.\n");
        return;
    }
    CloseHandle(g_worker_thread);
    CloseHandle(g_job_event);
    CloseHandle(g_quit_event);
    DeleteCriticalSection(&g_worker_lock);
    g_worker_thread = NULL;
    g_job_event = NULL;
    g_quit_event = NULL;
    g_worker_lock_initialized = FALSE;
    g_worker_started = FALSE;
    g_hwnd_notify = NULL;
}
