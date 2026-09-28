#include "compare.h"

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "metrics_async.h"
#include "report.h"
#include "ranking.h"

static HWND s_windows[CMP_MAX_WINDOWS];
static int s_window_count;
static BOOL s_metrics_stage2 = TRUE;
static HFONT s_font;
static const wchar_t (*s_drop_sort_paths)[MAX_PATH];

static double dmin(double a, double b)
{
    return a < b ? a : b;
}

static double dmax(double a, double b)
{
    return a > b ? a : b;
}

static double clamp_double(double value, double minimum, double maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

HFONT Compare_Font(void)
{
    return s_font ? s_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

HFONT Cmp_UiFont(void)
{
    return Compare_Font();
}

void Compare_SetFont(HFONT font)
{
    s_font = font;
}

BOOL wide_to_acp_strict(const wchar_t *wide, char *path, size_t capacity)
{
    wchar_t roundtrip[MAX_PATH];
    BOOL used_default = FALSE;
    UINT code_page = GetACP();
    DWORD flags = code_page == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL *used = code_page == CP_UTF8 ? NULL : &used_default;
    int bytes, wide_chars;

    if (!wide || !wide[0] || !path || capacity == 0 ||
        capacity > INT_MAX || wcschr(wide, L'?'))
        return FALSE;
    bytes = WideCharToMultiByte(code_page, flags, wide, -1, path,
                                (int)capacity, NULL, used);
    if (bytes <= 0 || used_default || strchr(path, '?'))
        return FALSE;
    wide_chars = MultiByteToWideChar(code_page, 0, path, -1, roundtrip,
                                     MAX_PATH);
    return wide_chars > 0 && lstrcmpW(wide, roundtrip) == 0;
}

static int cmp_drop_compare(const void *left, const void *right)
{
    UINT a = *(const UINT *)left;
    UINT b = *(const UINT *)right;
    return StrCmpLogicalW(s_drop_sort_paths[a], s_drop_sort_paths[b]);
}

BOOL CmpDrop_Collect(HDROP drop, char paths[CMP_MAX_CELLS][MAX_PATH],
                     int capacity, cmp_drop_stats_t *stats)
{
    UINT file_count, i, candidate_count = 0;
    UINT *order = NULL;
    wchar_t (*wide_paths)[MAX_PATH] = NULL;
    wchar_t (*sorted_paths)[MAX_PATH] = NULL;
    cmp_drop_stats_t result;
    int count = 0;

    ZeroMemory(&result, sizeof(result));
    if (stats)
        *stats = result;
    if (!drop || !paths || capacity <= 0)
        return FALSE;
    if (capacity > CMP_MAX_CELLS)
        capacity = CMP_MAX_CELLS;
    file_count = DragQueryFileW(drop, 0xffffffffu, NULL, 0);
    result.input = file_count;
    if (file_count) {
        wide_paths = (wchar_t (*)[MAX_PATH])calloc(file_count,
                                                   sizeof(*wide_paths));
        order = (UINT *)malloc((size_t)file_count * sizeof(*order));
        if (!wide_paths || !order) {
            result.allocation_failed = TRUE;
            goto done;
        }
    }
    for (i = 0; i < file_count; i++) {
        wchar_t path[MAX_PATH];
        DWORD attributes;
        UINT length = DragQueryFileW(drop, i, path, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            result.unsupported++;
            continue;
        }
        attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            result.directories++;
            continue;
        }
        {
            const wchar_t *extension = PathFindExtensionW(path);
            if (_wcsicmp(extension, L".png") != 0 &&
                _wcsicmp(extension, L".jpg") != 0 &&
                _wcsicmp(extension, L".jpeg") != 0 &&
                _wcsicmp(extension, L".bmp") != 0) {
                result.unsupported++;
                continue;
            }
        }
        memcpy(wide_paths[candidate_count], path,
               ((size_t)length + 1) * sizeof(wchar_t));
        order[candidate_count] = candidate_count;
        candidate_count++;
    }
    s_drop_sort_paths = (const wchar_t (*)[MAX_PATH])wide_paths;
    if (candidate_count > 1)
        qsort(order, candidate_count, sizeof(*order), cmp_drop_compare);
    sorted_paths = (wchar_t (*)[MAX_PATH])calloc(candidate_count,
                                                 sizeof(*sorted_paths));
    if (candidate_count && !sorted_paths) {
        result.allocation_failed = TRUE;
        goto done;
    }
    for (i = 0; i < candidate_count; i++)
        memcpy(sorted_paths[i], wide_paths[order[i]], sizeof(*sorted_paths));
    s_drop_sort_paths = (const wchar_t (*)[MAX_PATH])sorted_paths;
    for (i = 0; i < candidate_count; i++) {
        int previous;
        char converted[MAX_PATH];
        if (i && lstrcmpiW(sorted_paths[i - 1], sorted_paths[i]) == 0) {
            result.duplicates++;
            continue;
        }
        if (!wide_to_acp_strict(sorted_paths[i], converted,
                                sizeof(converted))) {
            result.non_acp++;
            continue;
        }
        for (previous = 0; previous < count; previous++)
            if (lstrcmpiA(paths[previous], converted) == 0)
                break;
        if (previous < count) {
            result.duplicates++;
            continue;
        }
        if (count == capacity) {
            result.truncated++;
            continue;
        }
        lstrcpynA(paths[count++], converted, MAX_PATH);
    }
    result.collected = (UINT)count;
done:
    s_drop_sort_paths = NULL;
    free(sorted_paths);
    free(order);
    free(wide_paths);
    if (stats)
        *stats = result;
    return count > 0;
}

BOOL Compare_CanOpen(int need)
{
    return need > 0 && CmpImage_LiveCount() <= CMP_MAX_LIVE_IMG - need;
}

BOOL CmpReg_Add(HWND hwnd)
{
    int i;
    for (i = 0; i < s_window_count; i++)
        if (s_windows[i] == hwnd)
            return TRUE;
    if (s_window_count >= CMP_MAX_WINDOWS)
        return FALSE;
    s_windows[s_window_count++] = hwnd;
    return TRUE;
}

void CmpReg_Remove(HWND hwnd)
{
    int i;
    for (i = 0; i < s_window_count; i++) {
        if (s_windows[i] == hwnd) {
            s_windows[i] = s_windows[--s_window_count];
            return;
        }
    }
}

BOOL Compare_PreTranslate(MSG *msg)
{
    HWND root;
    int i;
    if (!msg || msg->message != WM_KEYDOWN || !msg->hwnd)
        return FALSE;
    root = GetAncestor(msg->hwnd, GA_ROOT);
    for (i = 0; i < s_window_count; i++) {
        if (s_windows[i] == root)
            return SendMessageA(root, CMPM_KEY, msg->wParam,
                                (LPARAM)msg->hwnd) != 0;
    }
    return FALSE;
}

BOOL Compare_MetricsStage2Enabled(void)
{
    return s_metrics_stage2;
}

void Compare_SetMetricsStage2Enabled(BOOL enabled)
{
    s_metrics_stage2 = enabled != FALSE;
}

BOOL Compare_RunMetrics(HWND hwnd, cmp_image_t *const *images,
                        const RECT *roi_rects, int count,
                        metrics_async_job_t **async_job)
{
    metrics_item_result_t items[CMP_MAX_CELLS];
    BOOL large = FALSE;
    int i;
    HCURSOR previous_cursor;
    if (async_job)
        *async_job = NULL;
    if (!hwnd || !images || !roi_rects || count <= 0 ||
        count > CMP_MAX_CELLS) {
        if (hwnd)
            MessageBoxA(hwnd, "The visible image regions are not valid.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        int64_t width = (int64_t)roi_rects[i].right - roi_rects[i].left;
        int64_t height = (int64_t)roi_rects[i].bottom - roi_rects[i].top;
        if (!images[i] || !images[i]->img.valid || !images[i]->img.px ||
            width <= 0 || height <= 0) {
            MessageBoxA(hwnd, "The visible image regions are not valid.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        if ((uint64_t)width * (uint64_t)height >=
            (uint64_t)METRICS_ASYNC_THRESHOLD_PX)
            large = TRUE;
    }
    if (large) {
        metrics_async_job_t *job;
        if (!async_job) {
            MessageBoxA(hwnd, "Could not start background metrics analysis.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        job = Metrics_StartAsync(hwnd, WM_APP_METRICS_DONE, images,
                                 roi_rects, count, s_metrics_stage2);
        if (!job) {
            MessageBoxA(hwnd, "Could not start background metrics analysis.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        *async_job = job;
        return TRUE;
    }
    memset(items, 0, sizeof(items));
    previous_cursor = SetCursor(LoadCursorA(NULL, IDC_WAIT));
    {
        metrics_masks_t reference_masks;
        if (!MetricsMasks_Build(&images[0]->img, roi_rects[0],
                                &reference_masks)) {
            SetCursor(previous_cursor);
            MessageBoxA(hwnd, "Could not build reference metric masks.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        for (i = 0; i < count; i++) {
            if (!Metrics_AnalyzeROI_WithReferenceMask(
                    &images[i]->img, roi_rects[i], s_metrics_stage2,
                    &reference_masks, &items[i])) {
                int j;
                for (j = 0; j <= i; j++)
                    Metrics_FreeItemResult(&items[j]);
                MetricsMasks_Free(&reference_masks);
                SetCursor(previous_cursor);
                MessageBoxA(hwnd, "Metrics analysis failed.",
                            "Metrics Report", MB_OK | MB_ICONERROR);
                return FALSE;
            }
            lstrcpynA(items[i].image_name, images[i]->name,
                      (int)sizeof(items[i].image_name));
        }
        MetricsMasks_Free(&reference_masks);
    }
    SetCursor(previous_cursor);
    Metrics_ApplyReference(items, count, 0);
    MetricsRank_Compute(items, count, METRICS_PROFILE_BALANCED,
                        METRICS_RANK_ABSOLUTE_DEFAULT);
    {
        BOOL success = Report_GenerateAndOpen(
            items, count, "ROI Comparison Metrics", NULL);
        for (i = 0; i < count; i++)
            Metrics_FreeItemResult(&items[i]);
        return success;
    }
}

BOOL Compare_CompleteMetrics(metrics_async_job_t *job, BOOL *cancelled)
{
    const metrics_async_result_t *result = Metrics_AsyncResult(job);
    if (cancelled)
        *cancelled = result ? result->cancelled : FALSE;
    if (result && result->success && result->count > 0) {
        if (Report_GenerateAndOpen(result->items, result->count,
                                   "ROI Comparison Metrics", NULL))
            return TRUE;
        return FALSE;
    }
    if (result && !result->cancelled)
        MessageBoxA(NULL, "Metrics analysis failed.", "Metrics Report",
                    MB_OK | MB_ICONERROR);
    return result && result->cancelled;
}

BOOL Compare_TriggerMetrics(void)
{
    HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
    int i;
    for (i = s_window_count - 1; i >= 0; i--) {
        HWND target = s_windows[i];
        if (!IsWindow(target))
            continue;
        if (target == foreground) {
            SendMessageA(target, CMPM_METRICS, 0, 0);
            return TRUE;
        }
    }
    for (i = s_window_count - 1; i >= 0; i--) {
        HWND target = s_windows[i];
        if (IsWindow(target)) {
            SendMessageA(target, CMPM_METRICS, 0, 0);
            return TRUE;
        }
    }
    return FALSE;
}

void Compare_CloseAll(void)
{
    int guard = CMP_MAX_WINDOWS * 2;
    while (s_window_count > 0 && guard-- > 0) {
        HWND hwnd = s_windows[s_window_count - 1];
        if (!DestroyWindow(hwnd))
            CmpReg_Remove(hwnd);
    }
}

double CmpZoom_Quantize(double zoom)
{
    if (!isfinite(zoom))
        zoom = 1.0;
    zoom = floor(zoom * 100.0 + 0.5) / 100.0;
    return clamp_double(zoom, CMP_ZOOM_MIN, CMP_ZOOM_MAX);
}

double CmpZoom_Step(double zoom, int direction)
{
    double next;
    zoom = CmpZoom_Quantize(zoom);
    next = CmpZoom_Quantize(direction > 0 ?
                             zoom * CMP_ZOOM_STEP : zoom / CMP_ZOOM_STEP);
    if (fabs(next - zoom) < 1e-9)
        next = CmpZoom_Quantize(zoom + (direction > 0 ? 0.01 : -0.01));
    if ((zoom < 1.0 && next > 1.0) ||
        (zoom > 1.0 && next < 1.0))
        next = 1.0;
    return next;
}

void CmpView_Fit(cmp_view_t *view, int image_w, int image_h,
                 int view_w, int view_h, double margin)
{
    double zoom = 1.0;
    if (!view)
        return;
    if (image_w > 0 && image_h > 0 && view_w > 0 && view_h > 0) {
        zoom = dmin((double)view_w / image_w,
                    (double)view_h / image_h) * margin;
        zoom = floor(zoom * 100.0) / 100.0;
    }
    view->zoom = clamp_double(zoom, CMP_ZOOM_MIN, CMP_ZOOM_MAX);
    view->u = image_w * 0.5;
    view->v = image_h * 0.5;
}

void CmpView_ZoomAt(cmp_view_t *view, double zoom, double x, double y,
                    int view_w, int view_h)
{
    double dx, dy, image_x, image_y;
    if (!view || view->zoom <= 0.0)
        return;
    zoom = CmpZoom_Quantize(zoom);
    dx = x - view_w * 0.5;
    dy = y - view_h * 0.5;
    image_x = view->u + dx / view->zoom;
    image_y = view->v + dy / view->zoom;
    view->zoom = zoom;
    view->u = image_x - dx / zoom;
    view->v = image_y - dy / zoom;
}

void CmpView_Pan(cmp_view_t *view, double dx, double dy)
{
    if (!view || view->zoom <= 0.0)
        return;
    view->u -= dx / view->zoom;
    view->v -= dy / view->zoom;
}

static double clamp_axis(double center, int image_size, int viewport_size,
                         double zoom)
{
    double half;
    if (image_size <= 0 || viewport_size <= 0 || zoom <= 0.0)
        return image_size * 0.5;
    half = viewport_size * 0.5 / zoom;
    if (image_size * zoom <= viewport_size)
        return image_size * 0.5;
    return clamp_double(center, half, image_size - half);
}

void CmpView_ClampEdges(cmp_view_t *view, int image_w, int image_h,
                        int view_w, int view_h)
{
    if (!view)
        return;
    view->u = clamp_axis(view->u, image_w, view_w, view->zoom);
    view->v = clamp_axis(view->v, image_h, view_h, view->zoom);
}

void CmpView_ScreenToImage(const cmp_view_t *view, int view_w, int view_h,
                           double x, double y, double *image_x,
                           double *image_y)
{
    if (!view || !image_x || !image_y || view->zoom <= 0.0)
        return;
    *image_x = view->u + (x - view_w * 0.5) / view->zoom;
    *image_y = view->v + (y - view_h * 0.5) / view->zoom;
}

BOOL CmpView_VisibleRect(const cmp_view_t *view, int view_w, int view_h,
                         int image_w, int image_h, RECT *visible)
{
    double origin_x, origin_y, left, top, right, bottom;
    if (!view || !visible || view->zoom <= 0.0 ||
        view_w <= 0 || view_h <= 0 || image_w <= 0 || image_h <= 0)
        return FALSE;
    origin_x = view_w * 0.5 - view->u * view->zoom;
    origin_y = view_h * 0.5 - view->v * view->zoom;
    left = fmax(0.0, origin_x);
    top = fmax(0.0, origin_y);
    right = fmin((double)view_w, origin_x + image_w * view->zoom);
    bottom = fmin((double)view_h, origin_y + image_h * view->zoom);
    if (right <= left || bottom <= top)
        return FALSE;
    visible->left = (LONG)floor(left);
    visible->top = (LONG)floor(top);
    visible->right = (LONG)ceil(right);
    visible->bottom = (LONG)ceil(bottom);
    return TRUE;
}

void Cmp_Blit(HDC dc, cmp_image_t *image, const cmp_view_t *view,
              const RECT *viewport, const RECT *clip)
{
    cmp_level_t level;
    BITMAPINFO bitmap_info;
    RECT view_visible;
    double zoom, origin_x, origin_y, visible_left, visible_top;
    double visible_right, visible_bottom, level_zoom;
    int k = 0, level_count, saved;
    int sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1;

    if (!dc || !image || !image->img.valid || !view ||
        !viewport || !clip || view->zoom <= 0.0)
        return;
    if (!CmpView_VisibleRect(view,
            viewport->right - viewport->left,
            viewport->bottom - viewport->top,
            image->img.w, image->img.h, &view_visible) ||
        viewport->left + view_visible.right <= clip->left ||
        viewport->top + view_visible.bottom <= clip->top ||
        viewport->left + view_visible.left >= clip->right ||
        viewport->top + view_visible.top >= clip->bottom)
        return;
    zoom = view->zoom;
    origin_x = viewport->left +
               (viewport->right - viewport->left) * 0.5 - view->u * zoom;
    origin_y = viewport->top +
               (viewport->bottom - viewport->top) * 0.5 - view->v * zoom;
    visible_left = dmax((double)clip->left, origin_x);
    visible_top = dmax((double)clip->top, origin_y);
    visible_right = dmin((double)clip->right,
                         origin_x + image->img.w * zoom);
    visible_bottom = dmin((double)clip->bottom,
                          origin_y + image->img.h * zoom);
    if (visible_right <= visible_left || visible_bottom <= visible_top)
        return;

    if (zoom < 1.0) {
        level_count = CmpImage_Levels(image);
        while (k + 1 < level_count &&
               zoom * (double)(1 << (k + 1)) <= 1.0)
            k++;
    }
    if (!CmpImage_Level(image, k, &level) &&
        !CmpImage_Level(image, 0, &level))
        return;
    level_zoom = zoom * (double)(1 << level.shift);
    sx0 = (int)floor((visible_left - origin_x) / level_zoom);
    sy0 = (int)floor((visible_top - origin_y) / level_zoom);
    sx1 = (int)ceil((visible_right - origin_x) / level_zoom);
    sy1 = (int)ceil((visible_bottom - origin_y) / level_zoom);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > level.w) sx1 = level.w;
    if (sy1 > level.h) sy1 = level.h;
    if (sx1 <= sx0 || sy1 <= sy0)
        return;

    dx0 = (int)floor(origin_x + sx0 * level_zoom + 0.5);
    dy0 = (int)floor(origin_y + sy0 * level_zoom + 0.5);
    dx1 = (int)floor(origin_x + sx1 * level_zoom + 0.5);
    dy1 = (int)floor(origin_y + sy1 * level_zoom + 0.5);
    if (dx1 <= dx0) dx1 = dx0 + 1;
    if (dy1 <= dy0) dy1 = dy0 + 1;

    saved = SaveDC(dc);
    if (saved == 0)
        return;
    IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
    if (level_zoom < 1.0) {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
    } else {
        SetStretchBltMode(dc, COLORONCOLOR);
    }
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = level.w;
    bitmap_info.bmiHeader.biHeight = -(sy1 - sy0);
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                  sx0, 0, sx1 - sx0, sy1 - sy0,
                  level.px + (size_t)sy0 * (size_t)level.pitch,
                  &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(dc, saved);
}
