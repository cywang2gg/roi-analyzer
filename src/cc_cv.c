#include <stdio.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cc_cv.h"

static cc_cv_api_t g_cv_api;

static void *cv_proc(HMODULE module, const char *name)
{
    FARPROC proc;

    if (module == NULL || name == NULL) {
        return NULL;
    }
    proc = GetProcAddress(module, name);
    if (proc == NULL) {
        return NULL;
    }
    return (void *)(void (*)(void))proc;
}

static int load_from(const char *dir, size_t dir_length)
{
    char core_path[MAX_PATH];
    char imgproc_path[MAX_PATH];
    size_t need;

    need = dir_length + sizeof("libopencv_core-412.dll");
    if (need > sizeof(core_path)) {
        return 0;
    }
    memcpy(core_path, dir, dir_length);
    memcpy(core_path + dir_length, "libopencv_core-412.dll",
           sizeof("libopencv_core-412.dll"));
    memcpy(imgproc_path, dir, dir_length);
    memcpy(imgproc_path + dir_length, "libopencv_imgproc-412.dll",
           sizeof("libopencv_imgproc-412.dll"));
    g_cv_api.core = LoadLibraryExA(core_path, NULL,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_cv_api.core == NULL) {
        return 0;
    }
    g_cv_api.imgproc = LoadLibraryExA(imgproc_path, NULL,
                                      LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_cv_api.imgproc == NULL) {
        FreeLibrary(g_cv_api.core);
        g_cv_api.core = NULL;
        return 0;
    }
    return 1;
}

int cc_cv_load(void)
{
    char exe_path[MAX_PATH];
    char *slash;
    size_t dir_length;
    DWORD length;

    if (g_cv_api.loaded) {
        return 1;
    }
    memset(&g_cv_api, 0, sizeof(g_cv_api));
    length = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return 0;
    }
    slash = strrchr(exe_path, '\\');
    if (slash == NULL) {
        slash = strrchr(exe_path, '/');
    }
    if (slash == NULL) {
        return 0;
    }
    dir_length = (size_t)(slash - exe_path) + 1U;
    if (!load_from(exe_path, dir_length) && !load_from("", 0)) {
        return 0;
    }
    g_cv_api.cv_cvt_color =
        (cc_pfn_cvt_color)cv_proc(g_cv_api.imgproc, "cvCvtColor");
    g_cv_api.cv_smooth =
        (cc_pfn_smooth)cv_proc(g_cv_api.imgproc, "cvSmooth");
    g_cv_api.cv_canny =
        (cc_pfn_canny)cv_proc(g_cv_api.imgproc, "cvCanny");
    g_cv_api.cv_hough_lines2 =
        (cc_pfn_hough_lines2)cv_proc(g_cv_api.imgproc, "cvHoughLines2");
    g_cv_api.cv_create_image =
        (cc_pfn_create_image)cv_proc(g_cv_api.core, "cvCreateImage");
    g_cv_api.cv_release_image =
        (cc_pfn_release_image)cv_proc(g_cv_api.core, "cvReleaseImage");
    g_cv_api.cv_create_mem_storage =
        (cc_pfn_create_mem_storage)cv_proc(g_cv_api.core,
                                           "cvCreateMemStorage");
    g_cv_api.cv_release_mem_storage =
        (cc_pfn_release_mem_storage)cv_proc(g_cv_api.core,
                                            "cvReleaseMemStorage");
    g_cv_api.cv_get_seq_elem =
        (cc_pfn_get_seq_elem)cv_proc(g_cv_api.core, "cvGetSeqElem");
    if (g_cv_api.cv_cvt_color == NULL || g_cv_api.cv_smooth == NULL ||
        g_cv_api.cv_canny == NULL || g_cv_api.cv_hough_lines2 == NULL ||
        g_cv_api.cv_create_image == NULL ||
        g_cv_api.cv_release_image == NULL ||
        g_cv_api.cv_create_mem_storage == NULL ||
        g_cv_api.cv_release_mem_storage == NULL ||
        g_cv_api.cv_get_seq_elem == NULL) {
        cc_cv_unload();
        return 0;
    }
    g_cv_api.loaded = 1;
    return 1;
}

void cc_cv_unload(void)
{
    if (g_cv_api.imgproc != NULL) {
        FreeLibrary(g_cv_api.imgproc);
    }
    if (g_cv_api.core != NULL) {
        FreeLibrary(g_cv_api.core);
    }
    memset(&g_cv_api, 0, sizeof(g_cv_api));
}

const cc_cv_api_t *cc_cv_api(void)
{
    return g_cv_api.loaded ? &g_cv_api : NULL;
}

static int valid_image(cc_image_view_t image, int channels)
{
    if (image.pixels == NULL || image.width <= 0 || image.height <= 0 ||
        image.channels != channels || image.width > INT_MAX / channels ||
        image.stride < image.width * channels || image.stride <= 0) {
        return 0;
    }
    return (size_t)image.height <= SIZE_MAX / (size_t)image.stride;
}

static IplImage *copy_to_ipl(const cc_cv_api_t *api, cc_image_view_t source)
{
    IplImage *image;
    int y;

    image = api->cv_create_image(cvSize(source.width, source.height),
                                 IPL_DEPTH_8U, source.channels);
    if (image == NULL) {
        return NULL;
    }
    for (y = 0; y < source.height; ++y) {
        memcpy(image->imageData + (size_t)y * (size_t)image->widthStep,
               source.pixels + (size_t)y * (size_t)source.stride,
               (size_t)source.width * (size_t)source.channels);
    }
    return image;
}

/* BORDER_REFLECT_101 index map: -2->2, -1->1, w->w-2, w+1->w-3 */
static int reflect_101(int p, int len)
{
    if (len <= 1) {
        return 0;
    }
    if (p < 0) {
        p = -p;
    }
    if (p >= len) {
        p = 2 * len - p - 2;
    }
    if (p < 0) {
        p = 0;
    }
    if (p >= len) {
        p = len - 1;
    }
    return p;
}

int cc_gray_blur_canny(cc_image_view_t bgr, cc_image_t *edge)
{
    const cc_cv_api_t *api;
    IplImage *source = NULL;
    IplImage *gray = NULL;
    IplImage *padded = NULL;
    IplImage *blurred_pad = NULL;
    IplImage *edges = NULL;
    int y;

    if (edge == NULL || !valid_image(bgr, 3)) {
        return -1;
    }
    api = cc_cv_api();
    if (api == NULL) {
        return -1;
    }
    memset(edge, 0, sizeof(*edge));
    source = copy_to_ipl(api, bgr);
    gray = api->cv_create_image(cvSize(bgr.width, bgr.height), IPL_DEPTH_8U,
                                1);
    padded = api->cv_create_image(cvSize(bgr.width + CC_CV_BORDER * 2,
                                         bgr.height + CC_CV_BORDER * 2),
                                  IPL_DEPTH_8U, 1);
    blurred_pad = api->cv_create_image(cvSize(bgr.width + CC_CV_BORDER * 2,
                                              bgr.height + CC_CV_BORDER * 2),
                                       IPL_DEPTH_8U, 1);
    edges = api->cv_create_image(cvSize(bgr.width, bgr.height), IPL_DEPTH_8U,
                                 1);
    if (source == NULL || gray == NULL || padded == NULL ||
        blurred_pad == NULL || edges == NULL) {
        goto fail;
    }
    api->cv_cvt_color(source, gray, CV_BGR2GRAY);
    /* Manual REFLECT_101 expansion (cvSmooth uses REPLICATE internally,
       Python uses REFLECT_101; see D2). Avoids one extra full copy. */
    for (y = 0; y < bgr.height + CC_CV_BORDER * 2; ++y) {
        int x;
        int sy = reflect_101(y - CC_CV_BORDER, bgr.height);
        unsigned char *dst =
            (unsigned char *)padded->imageData +
            (size_t)y * (size_t)padded->widthStep;
        unsigned char *src =
            (unsigned char *)gray->imageData +
            (size_t)sy * (size_t)gray->widthStep;
        for (x = 0; x < bgr.width + CC_CV_BORDER * 2; ++x) {
            dst[x] = src[reflect_101(x - CC_CV_BORDER, bgr.width)];
        }
    }
    api->cv_smooth(padded, blurred_pad, CV_GAUSSIAN, 5, 5, 0.0, 0.0);
    /* Crop center back to original size before Canny */
    for (y = 0; y < bgr.height; ++y) {
        memcpy(gray->imageData + (size_t)y * (size_t)gray->widthStep,
               blurred_pad->imageData +
                   (size_t)(y + CC_CV_BORDER) *
                       (size_t)blurred_pad->widthStep + CC_CV_BORDER,
               (size_t)bgr.width);
    }
    api->cv_canny(gray, edges, 50.0, 150.0, 3);
    if ((size_t)bgr.width > SIZE_MAX / (size_t)bgr.height) {
        goto fail;
    }
    edge->pixels = (uint8_t *)malloc((size_t)bgr.width *
                                     (size_t)bgr.height);
    if (edge->pixels == NULL) {
        goto fail;
    }
    edge->width = bgr.width;
    edge->height = bgr.height;
    edge->stride = bgr.width;
    edge->channels = 1;
    for (y = 0; y < bgr.height; ++y) {
        memcpy(edge->pixels + (size_t)y * (size_t)edge->stride,
               edges->imageData + (size_t)y * (size_t)edges->widthStep,
               (size_t)bgr.width);
    }
    api->cv_release_image(&edges);
    api->cv_release_image(&blurred_pad);
    api->cv_release_image(&padded);
    api->cv_release_image(&gray);
    api->cv_release_image(&source);
    return 0;

fail:
    if (edges != NULL) {
        api->cv_release_image(&edges);
    }
    if (blurred_pad != NULL) {
        api->cv_release_image(&blurred_pad);
    }
    if (padded != NULL) {
        api->cv_release_image(&padded);
    }
    if (gray != NULL) {
        api->cv_release_image(&gray);
    }
    if (source != NULL) {
        api->cv_release_image(&source);
    }
    free(edge->pixels);
    memset(edge, 0, sizeof(*edge));
    return -1;
}

int cc_hough_lines_p(cc_image_view_t edge, cc_segment_t *segments,
                     size_t segment_capacity, size_t *segment_count)
{
    const cc_cv_api_t *api;
    IplImage *source = NULL;
    CvMemStorage *storage = NULL;
    CvSeq *lines;
    size_t total;
    size_t count;
    size_t i;

    if (segment_count == NULL) {
        return -1;
    }
    *segment_count = 0;
    if (!valid_image(edge, 1) ||
        (segment_capacity != 0U && segments == NULL)) {
        return -1;
    }
    api = cc_cv_api();
    if (api == NULL) {
        return -1;
    }
    source = copy_to_ipl(api, edge);
    storage = api->cv_create_mem_storage(0);
    if (source == NULL || storage == NULL) {
        goto fail;
    }
    lines = api->cv_hough_lines2(source, storage, CV_HOUGH_PROBABILISTIC,
                                 1.0, CV_PI / 180.0, 40, 30.0, 10.0,
                                 0.0, CV_PI);
    /* Truncate only after reading total (see D2/M1) */
    total = lines != NULL ? (size_t)lines->total : 0U;
    count = total;
    if (count > segment_capacity) {
        count = segment_capacity;
    }
    if (count > CC_HOUGH_MAX_SEGMENTS) {
        count = CC_HOUGH_MAX_SEGMENTS;
    }
    for (i = 0; i < count; ++i) {
        const CvPoint *line =
            (const CvPoint *)api->cv_get_seq_elem(lines, (int)i);
        if (line == NULL) {
            goto fail;
        }
        segments[i].x1 = line[0].x;
        segments[i].y1 = line[0].y;
        segments[i].x2 = line[1].x;
        segments[i].y2 = line[1].y;
    }
    *segment_count = count;
    api->cv_release_mem_storage(&storage);
    api->cv_release_image(&source);
    return 0;

fail:
    if (storage != NULL) {
        api->cv_release_mem_storage(&storage);
    }
    if (source != NULL) {
        api->cv_release_image(&source);
    }
    return -1;
}
