#include "image.h"

#include <gdiplus/gdiplus.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Image_Load(image_t *img, const char *path)
{
    GpBitmap *bmp = NULL;
    GpStatus st;
    GdiplusStartupInput gi;
    ULONG_PTR token = 0;
    wchar_t wpath[MAX_PATH];
    UINT iw = 0, ih = 0;
    BitmapData locked;
    GpRect rc;
    unsigned char *buf = NULL;
    size_t rowbytes;
    int y;

    if (!img)
        return -1;
    ZeroMemory(img, sizeof(*img));
    if (!path || !path[0])
        return -1;

    if (Image_LoadWIC(img, path) == 0)
        return 0;

    gi.GdiplusVersion = 1;
    gi.DebugEventCallback = NULL;
    gi.SuppressBackgroundThread = FALSE;
    gi.SuppressExternalCodecs = FALSE;
    if (GdiplusStartup(&token, &gi, NULL) != Ok)
        return -1;

    /* Path comes from Win32 A-APIs (DragQueryFileA / GetOpenFileNameA / CLI),
       which use the system ANSI codepage — NOT UTF-8. CP_ACP keeps CJK paths intact. */
    if (MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH) <= 0) {
        GdiplusShutdown(token);
        return -1;
    }

    st = GdipCreateBitmapFromFile(wpath, &bmp);
    if (st != Ok || !bmp) {
        GdiplusShutdown(token);
        return -1;
    }
    GdipGetImageWidth((GpImage *)bmp, &iw);
    GdipGetImageHeight((GpImage *)bmp, &ih);
    if (iw == 0 || ih == 0 || iw > 16384 || ih > 16384) {
        GdipDisposeImage((GpImage *)bmp);
        GdiplusShutdown(token);
        return -1;
    }

    rc.X = 0;
    rc.Y = 0;
    rc.Width = (INT)iw;
    rc.Height = (INT)ih;
    st = GdipBitmapLockBits(bmp, &rc, ImageLockModeRead, PixelFormat32bppARGB, &locked);
    if (st != Ok) {
        GdipDisposeImage((GpImage *)bmp);
        GdiplusShutdown(token);
        return -1;
    }

    rowbytes = (size_t)iw * 4;
    buf = (unsigned char *)malloc(rowbytes * (size_t)ih);
    if (!buf) {
        GdipBitmapUnlockBits(bmp, &locked);
        GdipDisposeImage((GpImage *)bmp);
        GdiplusShutdown(token);
        return -1;
    }
    /* Locked pixels are 32bpp ARGB = BGRA byte order in memory: copy as-is. */
    if (locked.Stride > 0) {
        for (y = 0; y < (int)ih; y++)
            memcpy(buf + (size_t)y * rowbytes,
                   (const unsigned char *)locked.Scan0 + (size_t)y * (size_t)locked.Stride,
                   rowbytes);
    } else {
        size_t astride = (size_t)(-(locked.Stride));
        for (y = 0; y < (int)ih; y++)
            memcpy(buf + (size_t)y * rowbytes,
                   (const unsigned char *)locked.Scan0 + (size_t)((int)ih - 1 - y) * astride,
                   rowbytes);
    }
    GdipBitmapUnlockBits(bmp, &locked);
    GdipDisposeImage((GpImage *)bmp);
    GdiplusShutdown(token);

    img->px = buf;
    img->w = (int)iw;
    img->h = (int)ih;
    img->pitch = (int)rowbytes;
    strncpy(img->path, path, MAX_PATH - 1);
    img->path[MAX_PATH - 1] = '\0';
    strcpy(img->decoder, "GDI+");
    img->valid = TRUE;
    return 0;
}

BOOL Image_Clone(image_t *dst, const image_t *src)
{
    size_t bytes;

    if (!dst || dst == src)
        return FALSE;
    ZeroMemory(dst, sizeof(*dst));
    if (!src || !src->valid || !src->px || src->w <= 0 || src->h <= 0 ||
        src->w > INT_MAX / 4 || src->pitch < src->w * 4 ||
        (size_t)src->pitch > SIZE_MAX / (size_t)src->h)
        return FALSE;
    bytes = (size_t)src->pitch * (size_t)src->h;
    dst->px = (unsigned char *)malloc(bytes);
    if (!dst->px)
        return FALSE;
    memcpy(dst->px, src->px, bytes);
    dst->w = src->w;
    dst->h = src->h;
    dst->pitch = src->pitch;
    lstrcpynA(dst->path, src->path, MAX_PATH);
    lstrcpynA(dst->decoder, src->decoder, (int)sizeof(dst->decoder));
    dst->valid = TRUE;
    return TRUE;
}

void Image_Free(image_t *img)
{
    if (!img)
        return;
    if (img->px)
        free(img->px);
    img->px = NULL;
    img->w = img->h = img->pitch = 0;
    img->path[0] = '\0';
    img->decoder[0] = '\0';
    img->valid = FALSE;
}
