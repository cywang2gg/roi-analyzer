#include "yolo_post.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define IMG_W 480
#define IMG_H 744
#define LB_SCALE 0.86021505376
#define LB_PAD_X 113
#define LB_PAD_Y 0

static float box_iou(float ax0, float ay0, float ax1, float ay1,
                     float bx0, float by0, float bx1, float by1)
{
    float ix0 = (ax0 > bx0) ? ax0 : bx0;
    float iy0 = (ay0 > by0) ? ay0 : by0;
    float ix1 = (ax1 < bx1) ? ax1 : bx1;
    float iy1 = (ay1 < by1) ? ay1 : by1;
    float iw = ix1 - ix0;
    float ih = iy1 - iy0;
    float inter;
    float uni;

    if (iw <= 0.0f || ih <= 0.0f) {
        return 0.0f;
    }
    inter = iw * ih;
    uni = (ax1 - ax0) * (ay1 - ay0) + (bx1 - bx0) * (by1 - by0) - inter;
    return (uni > 0.0f) ? inter / uni : 0.0f;
}

int main(void)
{
    static unsigned char rgb[YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U];
    static float out[YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES];
    FILE *f;
    size_t n;
    yolo_letterbox_t params;
    yolo_detection_t dets[8];
    size_t count = 0;
    float iou;

    /* Prediction values dumped by Python ORT (1-1.jpg, class color-chart) */
    const float exp_x0 = 102.404335f;
    const float exp_y0 = 194.220245f;
    const float exp_x1 = 370.975677f;
    const float exp_y1 = 551.823059f;

    f = fopen("ort_out_1-1.f32", "rb");
    if (f == NULL) {
        (void)puts("FAIL: cannot open ort_out_1-1.f32");
        return 1;
    }
    n = fread(out, sizeof(float),
              (size_t)YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES, f);
    fclose(f);
    if (n != (size_t)YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES) {
        (void)puts("FAIL: short read on ort_out_1-1.f32");
        return 1;
    }

    params.image_width = IMG_W;
    params.image_height = IMG_H;
    params.resized_width = 413;
    params.resized_height = 640;
    params.pad_x = LB_PAD_X;
    params.pad_y = LB_PAD_Y;
    params.scale = LB_SCALE;
    (void)rgb;

    if (yolo_decode_v8(out, &params, 0.25f, 0.70f, 100, dets, 8, &count) != 0) {
        (void)puts("FAIL: yolo_decode_v8 error");
        return 1;
    }
    (void)printf("C decode count=%u\n", (unsigned)count);
    if (count != 1) {
        (void)puts("FAIL: expected exactly 1 box");
        return 1;
    }
    (void)printf("C box: %.2f %.2f %.2f %.2f score=%.4f class=%d\n",
                 dets[0].x1, dets[0].y1, dets[0].x2, dets[0].y2,
                 dets[0].score, dets[0].class_id);
    (void)printf("PY box: %.2f %.2f %.2f %.2f\n",
                 exp_x0, exp_y0, exp_x1, exp_y1);
    iou = box_iou(dets[0].x1, dets[0].y1, dets[0].x2, dets[0].y2,
                  exp_x0, exp_y0, exp_x1, exp_y1);
    (void)printf("IoU=%.4f (need >0.9)\n", iou);
    if (iou <= 0.9f) {
        (void)puts("FAIL: IoU too low");
        return 1;
    }
    (void)puts("T11-E2E: PASS");
    return 0;
}
