#ifndef ROI_DETECT_WORKER_H
#define ROI_DETECT_WORKER_H

#include <windows.h>

#include "yolo_post.h"

typedef struct {
    LONG seq;
    int image_width;
    int image_height;
    yolo_letterbox_t transform;
    unsigned char *rgb;
} detect_job_t;

BOOL DetectWorker_Start(HWND hwnd_notify, char *error, size_t error_capacity);
BOOL DetectWorker_Submit(detect_job_t *job);
void DetectWorker_CancelAll(void);
void DetectWorker_Stop(void);

#endif
