#ifndef ROI_IMAGE_H
#define ROI_IMAGE_H

#include <windows.h>

// Image buffer: decoded to 32bpp BGRA (px[0]=B, px[1]=G, px[2]=R).
typedef struct image_s {
    unsigned char *px;
    int w, h, pitch;       // pitch = w * 4
    char path[MAX_PATH];
    char decoder[8];
    BOOL valid;
} image_t;

int Image_LoadWIC(image_t *img, const char *path);
// Load PNG/JPG/BMP via WIC, falling back to GDI+. Clears out on entry.
int Image_Load(image_t *img, const char *path);
void Image_Free(image_t *img);

#endif
