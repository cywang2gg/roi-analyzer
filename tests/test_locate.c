#define COBJMACROS

#include "cc_locate.h"
#include "image.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_u16(uint8_t *output, unsigned int value)
{
    output[0] = (uint8_t)(value & 255U);
    output[1] = (uint8_t)((value >> 8) & 255U);
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 255U);
    output[1] = (uint8_t)((value >> 8) & 255U);
    output[2] = (uint8_t)((value >> 16) & 255U);
    output[3] = (uint8_t)((value >> 24) & 255U);
}

static int dump_bmp(const char *input_path, const char *output_path)
{
    image_t image;
    FILE *output;
    uint8_t header[54];
    size_t row_bytes;
    size_t padded_row_bytes;
    size_t image_bytes;
    int y;
    int result = -1;
    HRESULT com_result;

    memset(&image, 0, sizeof(image));
    com_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) {
        (void)fprintf(stderr, "COM initialization failed: 0x%08lx\n",
                      (unsigned long)com_result);
        return -1;
    }
    if (Image_LoadWIC(&image, input_path) != 0) {
        (void)fprintf(stderr, "WIC decode failed: %s\n", input_path);
        CoUninitialize();
        return -1;
    }
    row_bytes = (size_t)image.w * 3U;
    padded_row_bytes = (row_bytes + 3U) & ~(size_t)3U;
    image_bytes = padded_row_bytes * (size_t)image.h;
    if (image_bytes > UINT32_MAX - sizeof(header)) {
        (void)fprintf(stderr, "BMP image is too large\n");
        goto done;
    }
    output = fopen(output_path, "wb");
    if (output == NULL) {
        (void)fprintf(stderr, "Cannot write BMP: %s\n", output_path);
        goto done;
    }
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    write_u32(header + 2, (uint32_t)(sizeof(header) + image_bytes));
    write_u32(header + 10, (uint32_t)sizeof(header));
    write_u32(header + 14, 40U);
    write_u32(header + 18, (uint32_t)image.w);
    write_u32(header + 22, (uint32_t)image.h);
    write_u16(header + 26, 1U);
    write_u16(header + 28, 24U);
    write_u32(header + 34, (uint32_t)image_bytes);
    if (fwrite(header, sizeof(header), 1U, output) != 1U) {
        (void)fprintf(stderr, "Cannot write BMP header\n");
        (void)fclose(output);
        goto done;
    }
    for (y = image.h - 1; y >= 0; --y) {
        const uint8_t *row = image.px + (size_t)y * (size_t)image.pitch;
        size_t x;

        for (x = 0; x < (size_t)image.w; ++x) {
            if (fwrite(row + x * 4U, 3U, 1U, output) != 1U) {
                (void)fprintf(stderr, "Cannot write BMP pixels\n");
                (void)fclose(output);
                goto done;
            }
        }
        if (padded_row_bytes > row_bytes) {
            const uint8_t padding[3] = { 0, 0, 0 };
            const size_t padding_size = padded_row_bytes - row_bytes;
            if (fwrite(padding, padding_size, 1U, output) != 1U) {
                (void)fprintf(stderr, "Cannot write BMP padding\n");
                (void)fclose(output);
                goto done;
            }
        }
    }
    if (fclose(output) != 0) {
        (void)fprintf(stderr, "Cannot finish BMP: %s\n", output_path);
        goto done;
    }
    (void)printf("WIC %d x %d -> %s\n", image.w, image.h, output_path);
    result = 0;

done:
    free(image.px);
    CoUninitialize();
    return result;
}

static void test_np_slice(void)
{
    int start;

    assert(cc_np_slice(-2, -1, 10, &start) == 1);
    assert(start == 8);
    assert(cc_np_slice(-20, 20, 10, &start) == 10);
    assert(start == 0);
    assert(cc_np_slice(12, 20, 10, &start) == 0);
    assert(start == 10);
    assert(cc_np_slice(8, 3, 10, &start) == -5);
    assert(start == 8);
}

static void test_refine_gap_and_spacing(void)
{
    const float positions[4] = { 0.0f, 10.0f, 40.0f, 50.0f };
    const float sparse[2] = { 10.0f, 30.0f };
    float output[5];
    float typical;
    size_t count;
    int good;

    assert(cc_refine_axis(positions, 4U, 100, 5U, 0.0f, 0,
                          output, 5U, &count, &typical, &good) == 0);
    assert(count == 5U);
    assert(fabsf(output[0] - 0.0f) < 0.001f);
    assert(fabsf(output[1] - 10.0f) < 0.001f);
    assert(fabsf(output[2] - 20.0f) < 0.001f);
    assert(fabsf(output[3] - 30.0f) < 0.001f);
    assert(fabsf(output[4] - 40.0f) < 0.001f);
    assert(cc_refine_axis(sparse, 2U, 100, 5U, 10.0f, 1,
                          output, 5U, &count, &typical, &good) == 0);
    assert(count == 5U);
    assert(!good);
    assert(fabsf(output[0] - 10.0f) < 0.001f);
    assert(fabsf(output[1] - 20.0f) < 0.001f);
    assert(fabsf(output[2] - 30.0f) < 0.001f);
    assert(fabsf(output[3] - 40.0f) < 0.001f);
    assert(fabsf(output[4] - 50.0f) < 0.001f);
}

static void test_refine_boundaries_and_orientation(void)
{
    const float one_position[1] = { 30.0f };
    const float tie_positions[6] = {
        0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f
    };
    const float horizontal_portrait[7] = {
        0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f
    };
    const float vertical_portrait[5] = {
        0.0f, 10.0f, 20.0f, 30.0f, 40.0f
    };
    float output[5];
    float typical;
    size_t count;
    int good;
    cc_grid_t grid;
    size_t i;

    assert(cc_refine_axis(one_position, 1U, 100, 5U, 0.0f, 0,
                          output, 5U, &count, &typical, &good) == 0);
    assert(count == 5U);
    assert(!good);
    assert(fabsf(typical - 20.0f) < 0.001f);
    assert(cc_refine_axis(NULL, 0U, 100, 5U, 0.0f, 0,
                          output, 5U, &count, &typical, &good) == 0);
    assert(count == 0U);
    assert(!good);
    assert(fabsf(typical - 20.0f) < 0.001f);
    assert(cc_grid_refine(horizontal_portrait, 7U, vertical_portrait, 5U,
                          100, 80, &grid) == 0);
    assert(grid.swapped);
    assert(grid.h_count == 7U);
    assert(grid.v_count == 5U);
    assert(grid.h_target == 7U);
    assert(grid.v_target == 5U);
    assert(grid.reason == NULL || grid.reason[0] == '\0');
    assert(cc_grid_refine(tie_positions, 6U, tie_positions, 6U,
                          100, 100, &grid) == 0);
    assert(!grid.swapped);
    assert(grid.h_count == CC_H_LINES_LAND);
    assert(grid.v_count == CC_V_LINES_LAND);
    for (i = 1; i < grid.h_count; ++i) {
        assert(grid.h[i] > grid.h[i - 1U]);
    }
}

static void test_canon_id(void)
{
    /* landscape 4x6: identity */
    assert(cc_canon_id(0, 0, 4, 6, 0) == 1);
    assert(cc_canon_id(0, 5, 4, 6, 0) == 6);
    assert(cc_canon_id(3, 0, 4, 6, 0) == 19);
    assert(cc_canon_id(3, 5, 4, 6, 0) == 24);
    /* portrait 6x4 image grid: rot=1 (cw90) maps image (0,0) to canon (3,0)=id19 */
    assert(cc_canon_id(0, 0, 6, 4, 1) == 19);
    assert(cc_canon_id(0, 3, 6, 4, 1) == 1);
    assert(cc_canon_id(5, 0, 6, 4, 1) == 24);
    assert(cc_canon_id(5, 3, 6, 4, 1) == 6);
    /* rot=3 (ccw90) maps image (0,0) to canon (0,5)=id6 */
    assert(cc_canon_id(0, 0, 6, 4, 3) == 6);
    assert(cc_canon_id(0, 3, 6, 4, 3) == 24);
    assert(cc_canon_id(5, 0, 6, 4, 3) == 1);
    assert(cc_canon_id(5, 3, 6, 4, 3) == 19);
    /* rot=2 (180) */
    assert(cc_canon_id(0, 0, 4, 6, 2) == 24);
    assert(cc_canon_id(3, 5, 4, 6, 2) == 1);
    /* out of range */
    assert(cc_canon_id(4, 0, 4, 6, 0) == -1);
    assert(cc_canon_id(0, 6, 4, 6, 0) == -1);
}

static void test_merge_and_roi_geometry(void)
{
    const cc_segment_t segments[4] = {
        { 0, 10, 100, 10 },
        { 0, 12, 100, 12 },
        { 20, 0, 20, 100 },
        { 22, 0, 22, 100 }
    };
    const float horizontal[5] = {
        0.0f, 10.0f, 20.0f, 30.0f, 40.0f
    };
    const float vertical[7] = {
        0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f
    };
    float merged_h[2];
    float merged_v[2];
    size_t h_count;
    size_t v_count;
    size_t valid_count;
    cc_roi_t rois[CC_ROI_COUNT];

    assert(cc_merge_positions(segments, 4U, 25.0f, merged_h, 2U, &h_count,
                              merged_v, 2U, &v_count) == 0);
    assert(h_count == 1U);
    assert(v_count == 1U);
    assert(fabsf(merged_h[0] - 11.0f) < 0.001f);
    assert(fabsf(merged_v[0] - 21.0f) < 0.001f);
    assert(cc_extract_rois(horizontal, 5U, vertical, 7U, 60, 40,
                           100, 200, 0.5f, 0.25f,
                           rois, &valid_count) == 0);
    assert(valid_count == 24U);
    assert(rois[0].id == 1);
    assert(rois[0].valid);
    assert(rois[0].x1 == 3);
    assert(rois[0].y1 == 3);
    assert(rois[0].x2 == 7);
    assert(rois[0].y2 == 7);
    assert(fabsf(rois[0].display_x1 - 106.0f) < 0.001f);
    assert(fabsf(rois[0].display_y1 - 212.0f) < 0.001f);
    assert(rois[23].id == 24);
}

static void test_crop_area_and_stats(void)
{
    const uint8_t bgra[16] = {
        10, 20, 30, 255, 20, 30, 40, 255,
        30, 40, 50, 255, 40, 50, 60, 255
    };
    const cc_image_view_t source = { bgra, 2, 2, 8, 4 };
    const cc_roi_t roi = { 1, 0, 0, 2, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f };
    cc_image_t cropped;
    cc_roi_stats_t stats;
    int origin_x;
    int origin_y;
    float rx;
    float ry;

    memset(&cropped, 0, sizeof(cropped));
    assert(cc_crop_scale(source, (cc_box_t){ 1.0f, 1.0f, 2.0f, 2.0f },
                         1.0f, 1, &cropped, &origin_x, &origin_y,
                         &rx, &ry) == 0);
    assert(cropped.width == 1);
    assert(cropped.height == 1);
    assert(cropped.channels == 3);
    assert(cropped.pixels[0] == 25U);
    assert(cropped.pixels[1] == 35U);
    assert(cropped.pixels[2] == 45U);
    assert(origin_x == 0);
    assert(origin_y == 0);
    assert(fabsf(rx - 0.5f) < 0.001f);
    assert(fabsf(ry - 0.5f) < 0.001f);
    {
        const cc_image_view_t crop_view = {
            cropped.pixels, cropped.width, cropped.height, cropped.stride,
            cropped.channels
        };
        const cc_roi_t crop_roi = {
            1, 0, 0, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f
        };
        assert(cc_roi_stats(crop_view, &crop_roi, &stats) == 0);
        assert(fabs(stats.mean_b - 25.0) < 0.001);
        assert(fabs(stats.mean_g - 35.0) < 0.001);
        assert(fabs(stats.mean_r - 45.0) < 0.001);
    }
    cc_image_free(&cropped);
    assert(cc_roi_stats(source, &roi, &stats) == 0);
    assert(fabs(stats.mean_b - 15.0) < 0.001);
    assert(fabs(stats.mean_g - 25.0) < 0.001);
    assert(fabs(stats.mean_r - 35.0) < 0.001);
    assert(fabs(stats.y_mean - 26.85) < 0.001);
    assert(fabs(stats.y_std - 5.0) < 0.001);
    assert(!stats.lab_valid);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--dump-bmp") == 0) {
        char output_path[MAX_PATH];
        const char *target = argc >= 4 ? argv[3] : NULL;

        if (target == NULL) {
            const char *extension = strrchr(argv[2], '.');
            size_t prefix = extension == NULL ? strlen(argv[2]) :
                            (size_t)(extension - argv[2]);
            if (prefix + 5U > sizeof(output_path)) {
                (void)fprintf(stderr, "Output path is too long\n");
                return 2;
            }
            memcpy(output_path, argv[2], prefix);
            memcpy(output_path + prefix, ".bmp", 5U);
            target = output_path;
        }
        return dump_bmp(argv[2], target) == 0 ? 0 : 1;
    }
    if (argc != 1) {
        (void)fprintf(stderr,
                      "Usage: test_locate [--dump-bmp input [output.bmp]]\n");
        return 2;
    }
    test_np_slice();
    test_refine_gap_and_spacing();
    test_refine_boundaries_and_orientation();
    test_canon_id();
    test_merge_and_roi_geometry();
    test_crop_area_and_stats();
    (void)puts("test_locate: all tests passed");
    return 0;
}
