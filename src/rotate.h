#ifndef ROI_ROTATE_H
#define ROI_ROTATE_H

#include <windows.h>

#include "image.h"

BOOL Image_Rotate90(image_t *img);
BOOL Image_Rotate180(image_t *img);
BOOL Image_Rotate270(image_t *img);
BOOL Image_RotateArbitrary(image_t *img, double angle_deg);

void ROI_RotateRect90(RECT *rc, int old_w, int old_h);
void ROI_RotateRect180(RECT *rc, int old_w, int old_h);
void ROI_RotateRect270(RECT *rc, int old_w, int old_h);

#endif
