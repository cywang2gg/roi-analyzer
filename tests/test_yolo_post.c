#include "yolo_post.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_letterbox_parameters(void)
{
    uint8_t *source = (uint8_t *)calloc((size_t)100 * 50 * 4U, 1U);
    uint8_t *output = (uint8_t *)malloc(
        (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U);
    yolo_letterbox_t params;

    assert(source != NULL);
    assert(output != NULL);
    assert(yolo_letterbox(source, 100, 50, 400, 0, output, &params) == 0);
    assert(params.image_width == 100);
    assert(params.image_height == 50);
    assert(params.resized_width == 640);
    assert(params.resized_height == 320);
    assert(params.pad_x == 0);
    assert(params.pad_y == 160);
    assert(fabs(params.scale - 6.4) < 0.000001);
    assert(output[0] == 114);
    assert(output[(size_t)160 * YOLO_INPUT_SIZE * 3U] == 0);
    free(output);
    free(source);
}

static void test_letterbox_rgb_and_orientation(void)
{
    uint8_t source[2U * 2U * 4U] = {
        10, 20, 30, 255, 40, 50, 60, 255,
        70, 80, 90, 255, 100, 110, 120, 255
    };
    uint8_t *output = (uint8_t *)malloc(
        (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * 3U);
    yolo_letterbox_t params;
    const size_t top_left = 0;
    const size_t bottom_left =
        (size_t)(YOLO_INPUT_SIZE - 1) * YOLO_INPUT_SIZE * 3U;

    assert(output != NULL);
    assert(yolo_letterbox(source, 2, 2, 8, 0, output, &params) == 0);
    assert(output[top_left] == 30);
    assert(output[top_left + 1U] == 20);
    assert(output[top_left + 2U] == 10);
    assert(yolo_letterbox(source, 2, 2, 8, 1, output, &params) == 0);
    assert(output[top_left] == 90);
    assert(output[bottom_left] == 30);
    free(output);
}

static void test_normalize_nchw(void)
{
    const size_t plane = (size_t)YOLO_INPUT_SIZE * YOLO_INPUT_SIZE;
    uint8_t *rgb = (uint8_t *)calloc(plane, 3U);
    float *nchw = (float *)calloc(plane * 3U, sizeof(*nchw));

    assert(rgb != NULL);
    assert(nchw != NULL);
    rgb[0] = 255;
    rgb[1] = 128;
    rgb[2] = 0;
    assert(yolo_normalize_nchw(rgb, nchw) == 0);
    assert(nchw[0] == 1.0f);
    assert(fabsf(nchw[plane] - (128.0f / 255.0f)) < 0.000001f);
    assert(nchw[plane * 2U] == 0.0f);
    free(nchw);
    free(rgb);
}

static void test_coordinate_restore(void)
{
    float output[YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES];
    yolo_letterbox_t params;
    yolo_detection_t detections[2];
    size_t count;
    const size_t index = 12;

    memset(output, 0, sizeof(output));
    params.image_width = 100;
    params.image_height = 80;
    params.resized_width = 200;
    params.resized_height = 160;
    params.pad_x = 10;
    params.pad_y = 20;
    params.scale = 2.0;
    output[index] = 100.0f;
    output[YOLO_OUTPUT_CANDIDATES + index] = 80.0f;
    output[2U * YOLO_OUTPUT_CANDIDATES + index] = 80.0f;
    output[3U * YOLO_OUTPUT_CANDIDATES + index] = 60.0f;
    output[5U * YOLO_OUTPUT_CANDIDATES + index] = 0.9f;
    assert(yolo_decode_v8(output, &params, 0.25f, 0.70f, 100,
                          detections, 2, &count) == 0);
    assert(count == 1);
    assert(fabsf(detections[0].x1 - 25.0f) < 0.001f);
    assert(fabsf(detections[0].y1 - 15.0f) < 0.001f);
    assert(fabsf(detections[0].x2 - 65.0f) < 0.001f);
    assert(fabsf(detections[0].y2 - 45.0f) < 0.001f);
}

static void test_iou(void)
{
    yolo_detection_t left = { 0.0f, 0.0f, 10.0f, 10.0f, 0.9f, 1 };
    yolo_detection_t right = { 5.0f, 5.0f, 15.0f, 15.0f, 0.8f, 1 };

    assert(fabsf(yolo_iou(&left, &right) - (25.0f / 175.0f)) < 0.0001f);
}

static void test_nms_overlap(void)
{
    yolo_detection_t detections[3] = {
        { 0.0f, 0.0f, 10.0f, 10.0f, 0.8f, 1 },
        { 1.0f, 1.0f, 11.0f, 11.0f, 0.9f, 1 },
        { 20.0f, 20.0f, 30.0f, 30.0f, 0.7f, 1 }
    };
    size_t kept;

    assert(yolo_nms(detections, 3, 0.5f, 100, &kept) == 0);
    assert(kept == 2);
    assert(detections[0].score == 0.9f);
    assert(detections[1].score == 0.7f);
}

static void test_nms_nonoverlap_and_classes(void)
{
    yolo_detection_t detections[3] = {
        { 0.0f, 0.0f, 10.0f, 10.0f, 0.8f, 1 },
        { 20.0f, 20.0f, 30.0f, 30.0f, 0.7f, 1 },
        { 0.0f, 0.0f, 10.0f, 10.0f, 0.6f, 0 }
    };
    size_t kept;

    assert(yolo_nms(detections, 3, 0.5f, 100, &kept) == 0);
    assert(kept == 3);
    assert(detections[0].class_id == 1);
    assert(detections[1].class_id == 1);
    assert(detections[2].class_id == 0);
}

static void test_empty_input(void)
{
    yolo_detection_t detection;
    size_t kept = 9;
    float output[YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES];
    yolo_letterbox_t params;
    size_t decoded = 9;

    assert(yolo_nms(NULL, 0, 0.7f, 100, &kept) == 0);
    assert(kept == 0);
    memset(output, 0, sizeof(output));
    params.image_width = 100;
    params.image_height = 100;
    params.resized_width = 640;
    params.resized_height = 640;
    params.pad_x = 0;
    params.pad_y = 0;
    params.scale = 6.4;
    assert(yolo_decode_v8(output, &params, 0.25f, 0.7f, 100,
                          &detection, 1, &decoded) == 0);
    assert(decoded == 0);
}

static void test_class_filter(void)
{
    float output[YOLO_OUTPUT_CHANNELS * YOLO_OUTPUT_CANDIDATES];
    yolo_letterbox_t params;
    yolo_detection_t detections[2];
    size_t count;
    const size_t rejected_index = 2;
    const size_t accepted_index = 3;

    memset(output, 0, sizeof(output));
    params.image_width = 640;
    params.image_height = 640;
    params.resized_width = 640;
    params.resized_height = 640;
    params.pad_x = 0;
    params.pad_y = 0;
    params.scale = 1.0;
    output[rejected_index] = 50.0f;
    output[YOLO_OUTPUT_CANDIDATES + rejected_index] = 50.0f;
    output[2U * YOLO_OUTPUT_CANDIDATES + rejected_index] = 20.0f;
    output[3U * YOLO_OUTPUT_CANDIDATES + rejected_index] = 20.0f;
    output[4U * YOLO_OUTPUT_CANDIDATES + rejected_index] = 0.9f;
    output[5U * YOLO_OUTPUT_CANDIDATES + rejected_index] = 0.1f;
    output[accepted_index] = 100.0f;
    output[YOLO_OUTPUT_CANDIDATES + accepted_index] = 100.0f;
    output[2U * YOLO_OUTPUT_CANDIDATES + accepted_index] = 20.0f;
    output[3U * YOLO_OUTPUT_CANDIDATES + accepted_index] = 20.0f;
    output[5U * YOLO_OUTPUT_CANDIDATES + accepted_index] = 0.8f;
    assert(yolo_decode_v8(output, &params, 0.25f, 0.7f, 100,
                          detections, 2, &count) == 0);
    assert(count == 1);
    assert(detections[0].class_id == 1);
    assert(detections[0].x1 == 90.0f);
}

int main(void)
{
    test_letterbox_parameters();
    test_letterbox_rgb_and_orientation();
    test_normalize_nchw();
    test_coordinate_restore();
    test_iou();
    test_nms_overlap();
    test_nms_nonoverlap_and_classes();
    test_empty_input();
    test_class_filter();
    (void)puts("test_yolo_post: all tests passed");
    return 0;
}
