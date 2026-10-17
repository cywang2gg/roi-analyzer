#include "pvt_locate.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 200
#define TEST_HEIGHT 200
#define TEST_STRIDE (TEST_WIDTH * 4)

static uint8_t image[TEST_HEIGHT * TEST_STRIDE];

static int failures;

static void report(const char *name, int passed)
{
    (void)printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
    {
        failures++;
    }
}

static void set_pixel(int x, int y, uint8_t value)
{
    size_t offset;

    offset = (size_t)y * (size_t)TEST_STRIDE + (size_t)x * 4U;
    image[offset] = value;
    image[offset + 1U] = value;
    image[offset + 2U] = value;
    image[offset + 3U] = 255U;
}

static void draw_quadrant_disk(int center_x, int center_y, int radius)
{
    int x;
    int y;

    for (y = center_y - radius; y <= center_y + radius; y++)
    {
        for (x = center_x - radius; x <= center_x + radius; x++)
        {
            int dx;
            int dy;
            int distance_squared;
            int radius_squared;
            uint8_t value;

            dx = x - center_x;
            dy = y - center_y;
            distance_squared = dx * dx + dy * dy;
            radius_squared = radius * radius;
            if (distance_squared > radius_squared)
            {
                continue;
            }
            if (distance_squared >= (radius - 3) * (radius - 3))
            {
                value = 0U;
            }
            else if (distance_squared < 64)
            {
                value = 0U;
            }
            else if ((dx < 0) == (dy < 0))
            {
                value = 255U;
            }
            else
            {
                value = 0U;
            }
            set_pixel(x, y, value);
        }
    }
}

static void draw_siemens_decoy(int center_x, int center_y, int radius)
{
    int x;
    int y;

    for (y = center_y - radius; y <= center_y + radius; y++)
    {
        for (x = center_x - radius; x <= center_x + radius; x++)
        {
            int dx;
            int dy;
            int distance_squared;
            int angle_bucket;
            double angle;

            dx = x - center_x;
            dy = y - center_y;
            distance_squared = dx * dx + dy * dy;
            if (distance_squared > radius * radius)
            {
                continue;
            }
            if (distance_squared >= (radius - 3) * (radius - 3))
            {
                set_pixel(x, y, 0U);
                continue;
            }
            angle = atan2((double)dy, (double)dx);
            angle_bucket = (int)floor((angle + 3.14159265358979323846) *
                                      12.0 / (2.0 * 3.14159265358979323846));
            set_pixel(x, y, (angle_bucket % 2 == 0) ? 0U : 255U);
        }
    }
}

static int locate_target(int center_x, int center_y, int radius,
                         pvt_target_t *target)
{
    pvt_box_t box;

    box.x1 = (float)(center_x - radius - 2);
    box.y1 = (float)(center_y - radius - 2);
    box.x2 = (float)(center_x + radius + 2);
    box.y2 = (float)(center_y + radius + 2);
    box.score = 1.0f;
    box.class_id = 3;
    return Pvt_LocateAndMeasure(image, TEST_WIDTH, TEST_HEIGHT, TEST_STRIDE,
                                &box, target);
}

static void draw_fractional_quadrant_disk(double center_x, double center_y,
                                         int radius)
{
    int x;
    int y;
    int sub_x;
    int sub_y;

    for (y = (int)floor(center_y - (double)radius - 1.0);
         y <= (int)ceil(center_y + (double)radius + 1.0); y++)
    {
        for (x = (int)floor(center_x - (double)radius - 1.0);
             x <= (int)ceil(center_x + (double)radius + 1.0); x++)
        {
            /*
             * 4x4 supersampling: a hard-thresholded renderer can only place a
             * seam on an integer boundary, so a fractional target centre would
             * not exist in the rendered image at all. Area coverage keeps the
             * rendered seam at the requested sub-pixel position.
             */
            double coverage_sum = 0.0;
            int covered = 0;

            if (x < 0 || x >= TEST_WIDTH || y < 0 || y >= TEST_HEIGHT)
            {
                continue;
            }
            for (sub_y = 0; sub_y < 4; sub_y++)
            {
                for (sub_x = 0; sub_x < 4; sub_x++)
                {
                    double sx;
                    double sy;
                    double dx;
                    double dy;
                    double distance_squared;
                    double value;

                    sx = (double)x + ((double)sub_x + 0.5) / 4.0;
                    sy = (double)y + ((double)sub_y + 0.5) / 4.0;
                    dx = sx - center_x;
                    dy = sy - center_y;
                    distance_squared = dx * dx + dy * dy;
                    if (distance_squared > (double)(radius * radius))
                    {
                        value = 128.0;
                    }
                    else if (distance_squared >=
                             (double)((radius - 3) * (radius - 3)))
                    {
                        value = 0.0;
                    }
                    else if ((dx < 0.0) == (dy < 0.0))
                    {
                        value = 255.0;
                    }
                    else
                    {
                        value = 0.0;
                    }
                    coverage_sum += value;
                    covered++;
                }
            }
            if (covered > 0)
            {
                set_pixel(x, y, (uint8_t)(coverage_sum / (double)covered + 0.5));
            }
        }
    }
}

static void draw_saddle_break_disk(int center_x, int center_y, int radius)
{
    int x;
    int y;

    for (y = center_y - radius; y <= center_y + radius; y++)
    {
        for (x = center_x - radius; x <= center_x + radius; x++)
        {
            int dx;
            int dy;
            int distance_squared;
            uint8_t value;

            if (x < 0 || x >= TEST_WIDTH || y < 0 || y >= TEST_HEIGHT)
            {
                continue;
            }
            dx = x - center_x;
            dy = y - center_y;
            distance_squared = dx * dx + dy * dy;
            if (distance_squared > radius * radius)
            {
                continue;
            }
            if (distance_squared <= 16)
            {
                value = 220U;
            }
            else if ((dx < 0) == (dy < 0))
            {
                value = 255U;
            }
            else
            {
                value = 0U;
            }
            set_pixel(x, y, value);
        }
    }
}

static void test_known_targets(void)
{
    pvt_target_t first;
    pvt_target_t second;
    pvt_target_t fractional;
    int first_ok;
    int second_ok;
    int fractional_ok;
    int centers_ok;
    int quadrant_ok;
    int lines_ok;

    memset(image, 0, sizeof(image));
    {
        int x;
        int y;

        for (y = 0; y < TEST_HEIGHT; y++)
        {
            for (x = 0; x < TEST_WIDTH; x++)
            {
                set_pixel(x, y, 128U);
            }
        }
    }
    draw_quadrant_disk(55, 62, 24);
    draw_quadrant_disk(145, 138, 17);
    draw_fractional_quadrant_disk(100.35, 96.70, 24);
    first_ok = locate_target(55, 62, 24, &first);
    second_ok = locate_target(145, 138, 17, &second);
    {
        pvt_box_t box;

        box.x1 = 73.0f;
        box.y1 = 70.0f;
        box.x2 = 128.0f;
        box.y2 = 123.0f;
        box.score = 1.0f;
        box.class_id = 3;
        fractional_ok = Pvt_LocateAndMeasure(image, TEST_WIDTH, TEST_HEIGHT,
                                             TEST_STRIDE, &box, &fractional);
    }
    report("quadrant disk radius 24 located", first_ok);
    report("quadrant disk radius 17 located", second_ok);
    report("fractional center disk located", fractional_ok);
    centers_ok = first_ok && second_ok && fractional_ok &&
        fabs((double)first.cx - 55.0) <= 0.20 &&
        fabs((double)first.cy - 62.0) <= 0.20 &&
        fabs((double)second.cx - 145.0) <= 0.20 &&
        fabs((double)second.cy - 138.0) <= 0.20 &&
        fabs((double)fractional.cx - 100.35) <= 0.20 &&
        fabs((double)fractional.cy - 96.70) <= 0.20;
    report("tight sub-pixel centers within 0.20 px", centers_ok);
    quadrant_ok = first_ok && second_ok && fractional_ok &&
        first.reason == NULL && second.reason == NULL &&
        fractional.reason == NULL;
    report("quadrant verification for all targets", quadrant_ok);
    lines_ok = first_ok && first.lines_valid == 1 &&
        fabs((double)first.line_v_slope) <= 0.45 &&
        fabs((double)first.line_h_slope) <= 0.45 &&
        fabs((double)first.line_v_slope * 62.0 +
             (double)first.line_v_offset - 55.0) <= 0.25 &&
        fabs((double)first.line_h_slope * 55.0 +
             (double)first.line_h_offset - 62.0) <= 0.25;
    report("fitted seam lines pass through known center", lines_ok);
}

static void test_blurred_and_gradient_targets(void)
{
    pvt_target_t target;
    int ok;
    int grad_ok;
    int x;
    int y;

    memset(image, 0, sizeof(image));
    for (y = 0; y < TEST_HEIGHT; y++)
    {
        for (x = 0; x < TEST_WIDTH; x++)
        {
            int gradient;
            int base;

            gradient = (int)(((double)x - (double)(TEST_WIDTH / 2)) * 40.0 /
                             (double)TEST_WIDTH);
            base = 128 + gradient;
            if (base < 0)
            {
                base = 0;
            }
            if (base > 255)
            {
                base = 255;
            }
            set_pixel(x, y, (uint8_t)base);
        }
    }
    draw_fractional_quadrant_disk(55.0, 62.0, 24);
    draw_fractional_quadrant_disk(145.0, 138.0, 17);
    ok = locate_target(55, 62, 24, &target);
    grad_ok = ok && target.reason == NULL &&
        fabs((double)target.cx - 55.0) <= 0.50 &&
        fabs((double)target.cy - 62.0) <= 0.50;
    report("blurred + gradient disk retained", grad_ok);
}

static void test_saddle_break_disk(void)
{
    pvt_target_t target;
    int located;
    int good;

    memset(image, 0, sizeof(image));
    {
        int x;
        int y;

        for (y = 0; y < TEST_HEIGHT; y++)
        {
            for (x = 0; x < TEST_WIDTH; x++)
            {
                set_pixel(x, y, 128U);
            }
        }
    }
    draw_saddle_break_disk(100, 100, 24);
    located = locate_target(100, 100, 24, &target);
    good = located && target.reason == NULL &&
        fabs((double)target.cx - 100.0) <= 0.50 &&
        fabs((double)target.cy - 100.0) <= 0.50;
    report("saddle-break disk recovered", good);
}

static void test_fit_line_intersection(void)
{
    double hx[10];
    double hy[10];
    double hw[10];
    double vx[10];
    double vy[10];
    double vw[10];
    double out_cx;
    double out_cy;
    int i;
    int ok;

    /*
     * Two physically meaningful seams of a quadrant target crossing at
     * (100, 100):  L_V: x = 0.12 * y + 88   and   L_H: y = 0.08 * x + 92.
     * Slopes stay inside the +/-0.45 rad gate the fitter enforces.
     */
    for (i = 0; i < 10; i++)
    {
        double y;
        double x;

        y = 100.0 + ((double)i - 4.5) * 15.0;
        x = 0.12 * y + 88.0;
        hx[i] = x;
        hy[i] = y;
        hw[i] = 100.0;
        {
            double vy_value;
            double vx_value;

            vx_value = 100.0 + ((double)i - 4.5) * 6.0;
            vy_value = 0.08 * vx_value + 92.0;
            vx[i] = vx_value;
            vy[i] = vy_value;
            vw[i] = 100.0;
        }
    }
    hx[2] += 8.0;
    vy[6] += 8.0;
    hw[5] = 15.0;
    ok = Pvt_FitLineIntersection(hx, hy, hw, 10, vx, vy, vw, 10,
                                 &out_cx, &out_cy);
    report("line-intersection fit rejects outliers", ok &&
           fabs(out_cx - 100.0) <= 0.01 &&
           fabs(out_cy - 100.0) <= 0.01);
}

static void test_decoy_and_background(void)
{
    pvt_target_t target;
    pvt_box_t box;
    int decoy_located;
    int background_located;
    int background_rejected;

    memset(image, 0, sizeof(image));
    {
        int x;
        int y;

        for (y = 0; y < TEST_HEIGHT; y++)
        {
            for (x = 0; x < TEST_WIDTH; x++)
            {
                set_pixel(x, y, 128U);
            }
        }
    }
    draw_siemens_decoy(100, 100, 22);
    decoy_located = locate_target(100, 100, 22, &target);
    report("Siemens-star decoy rejected", !decoy_located && target.reason != NULL);

    box.x1 = 15.0f;
    box.y1 = 15.0f;
    box.x2 = 45.0f;
    box.y2 = 45.0f;
    box.score = 1.0f;
    box.class_id = 3;
    background_located = Pvt_LocateAndMeasure(
        image, TEST_WIDTH, TEST_HEIGHT, TEST_STRIDE, &box, &target);
    background_rejected = !background_located && target.used == 0 &&
                          target.reason != NULL;
    report("background box rejected with reason", background_rejected);
}

static void test_profile_analysis(void)
{
    double clean[40];
    double overshoot[40];
    double ramp[40];
    double contrast;
    double overshoot_pct;
    double undershoot_pct;
    int index;
    int clean_ok;
    int overshoot_ok;
    int ramp_rejected;

    for (index = 0; index < 40; index++)
    {
        clean[index] = index < 20 ? 20.0 : 220.0;
        overshoot[index] = clean[index];
        ramp[index] = (double)index * 5.0;
    }
    overshoot[20] = 240.0;
    clean_ok = Pvt_AnalyzeProfile(clean, 40, &contrast, &overshoot_pct,
                                  &undershoot_pct) &&
        fabs(contrast - (200.0 / 240.0)) < 0.0001 &&
        overshoot_pct == 0.0 && undershoot_pct == 0.0;
    report("clean step profile", clean_ok);
    overshoot_ok = Pvt_AnalyzeProfile(overshoot, 40, &contrast, &overshoot_pct,
                                      &undershoot_pct) &&
        fabs(overshoot_pct - 10.0) < 0.01 &&
        undershoot_pct == 0.0;
    report("10 percent overshoot profile", overshoot_ok);
    ramp_rejected = !Pvt_AnalyzeProfile(ramp, 40, &contrast, &overshoot_pct,
                                        &undershoot_pct) &&
        contrast == 0.0 && overshoot_pct == 0.0 && undershoot_pct == 0.0;
    report("monotonic ramp rejected as incomplete", ramp_rejected);
}

int main(void)
{
    test_known_targets();
    test_decoy_and_background();
    test_profile_analysis();
    test_blurred_and_gradient_targets();
    test_saddle_break_disk();
    test_fit_line_intersection();
    if (failures != 0)
    {
        (void)printf("test_pvt: %d failure(s)\n", failures);
        return 1;
    }
    (void)puts("test_pvt: all tests passed");
    return 0;
}
