#ifndef ROI_IMAGE_WIC_H
#define ROI_IMAGE_WIC_H

#include <windows.h>
#include <stddef.h>

BOOL Image_EncodePNGMemory(const BYTE *bgra_pixels, int width, int height,
                           int stride, BYTE **out_png_data,
                           size_t *out_png_size);
void Image_FreePNGMemory(BYTE *png_data);

#endif
