#ifndef ROI_IMAGE_H
#define ROI_IMAGE_H

#include <windows.h>

// Image buffer: GDI+ loaded, converted to 32bpp BGRA (px[0]=B, px[1]=G, px[2]=R).
typedef struct image_s {
    unsigned char *px;
    int w, h, pitch;       // pitch = w * 4
    char path[MAX_PATH];
    BOOL valid;
} image_t;

// Load PNG/JPG/BMP via GDI+ flat API. Returns 0 on success. Replaces old content.
int Image_Load(image_t *img, const char *path);
void Image_Free(image_t *img);

#endif
