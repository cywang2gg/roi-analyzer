#ifndef ROI_YOLO_POST_H
#define ROI_YOLO_POST_H

#include <stddef.h>
#include <stdint.h>

#define YOLO_INPUT_SIZE 640
#define YOLO_OUTPUT_CHANNELS 6
#define YOLO_OUTPUT_CANDIDATES 8400
#define YOLO_DEFAULT_CONFIDENCE 0.25f
#define YOLO_DEFAULT_IOU 0.70f
#define YOLO_DEFAULT_MAX_DET 100

typedef struct {
    int image_width;
    int image_height;
    int resized_width;
    int resized_height;
    int pad_x;
    int pad_y;
    double scale;
} yolo_letterbox_t;

typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int class_id;
} yolo_detection_t;

int yolo_letterbox(const uint8_t *bgra, int width, int height, int stride,
                   int bottom_up, uint8_t *rgb, yolo_letterbox_t *params);
int yolo_normalize_nchw(const uint8_t *rgb, float *nchw);
float yolo_iou(const yolo_detection_t *left, const yolo_detection_t *right);
int yolo_nms(yolo_detection_t *detections, size_t count, float iou_threshold,
             size_t max_det, size_t *kept_count);
int yolo_decode_v8(const float *output, const yolo_letterbox_t *params,
                   float confidence_threshold, float iou_threshold,
                   size_t max_det, yolo_detection_t *detections,
                   size_t capacity, size_t *detection_count);

#endif
