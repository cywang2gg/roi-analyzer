#ifndef ROI_DETECT_H
#define ROI_DETECT_H

#include <windows.h>

#include "yolo_post.h"

#define IDT_DETECT_DELAY 2
#define DETECT_MAX_RESULTS YOLO_DEFAULT_MAX_DET
#define DETECT_RESULT_ALLOCATION_FAILURE 1

typedef enum {
    DETECT_DISABLED,
    DETECT_IDLE,
    DETECT_PENDING,
    DETECT_RUNNING,
    DETECT_DONE,
    DETECT_FAILED
} detect_state_t;

typedef struct {
    LONG seq;
    int status;
    size_t count;
    double elapsed_ms;
    char error[128];
    yolo_detection_t detections[DETECT_MAX_RESULTS];
} detect_result_t;

typedef struct {
    BOOL success;
    char error[128];
} detect_init_result_t;

BOOL Detect_Init(HWND hwnd_notify);
void Detect_OnImageUnloading(void);
void Detect_OnImageLoaded(void);
void Detect_OnTimer(const unsigned char *bgra, int width, int height,
                    int stride);
void Detect_OnInitResult(detect_init_result_t *result);
void Detect_OnResult(detect_result_t *result);
void Detect_OnResultAllocationFailure(LONG seq);
LONG Detect_ImageSeq(void);
void Detect_GetStatusText(char *text, size_t capacity);
size_t Detect_GetResults(const yolo_detection_t **detections);
void Detect_Shutdown(void);

#endif
