#include "compare.h"

#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG s_live_images;

static cmp_image_t *alloc_image(void)
{
    cmp_image_t *image = (cmp_image_t *)calloc(1, sizeof(*image));
    if (image) {
        image->refs = 1;
        InterlockedIncrement(&s_live_images);
    }
    return image;
}

static void set_name(cmp_image_t *image, const char *path)
{
    const char *base = PathFindFileNameA(path);
    const char *source = base ? base : path;
    const char *next;
    size_t length = 0;
    while (*source && length < MAX_PATH - 1) {
        next = CharNextA(source);
        if ((size_t)(next - source) > MAX_PATH - 1 - length)
            break;
        memcpy(image->name + length, source, (size_t)(next - source));
        length += (size_t)(next - source);
        source = next;
    }
    image->name[length] = '\0';
}

cmp_image_t *CmpImage_Load(const char *path)
{
    cmp_image_t *image;
    if (!path || !path[0])
        return NULL;
    image = alloc_image();
    if (!image)
        return NULL;
    if (Image_Load(&image->img, path) != 0 || !image->img.valid) {
        CmpImage_Unref(image);
        return NULL;
    }
    set_name(image, image->img.path);
    return image;
}

cmp_image_t *CmpImage_FromImage(const image_t *src)
{
    cmp_image_t *image;
    if (!src || !src->path[0])
        return NULL;
    image = alloc_image();
    if (!image)
        return NULL;
    if (!Image_Clone(&image->img, src)) {
        CmpImage_Unref(image);
        return NULL;
    }
    set_name(image, image->img.path);
    return image;
}

cmp_image_t *CmpImage_Ref(cmp_image_t *image)
{
    if (image)
        InterlockedIncrement(&image->refs);
    return image;
}

void CmpImage_Unref(cmp_image_t *image)
{
    if (!image)
        return;
    if (InterlockedDecrement(&image->refs) == 0) {
        ViewPyr_Free(&image->pyr);
        Image_Free(&image->img);
        free(image);
        InterlockedDecrement(&s_live_images);
    }
}

LONG CmpImage_LiveCount(void)
{
    return InterlockedCompareExchange(&s_live_images, 0, 0);
}

static void pyr_build(cmp_image_t *image)
{
    if (!image || image->pyr_built || image->pyr_failed)
        return;
    if (ViewPyr_Build(&image->pyr, &image->img))
        image->pyr_built = TRUE;
    else
        image->pyr_failed = TRUE;
}

static BOOL pyr_has_full(const cmp_image_t *image)
{
    const view_level_t *level;
    if (!image || !image->pyr_built || image->pyr.count <= 0)
        return FALSE;
    level = &image->pyr.levels[0];
    return level->px == image->img.px && level->w == image->img.w &&
           level->h == image->img.h && level->pitch == image->img.pitch;
}

int CmpImage_Levels(cmp_image_t *image)
{
    pyr_build(image);
    if (!image || !image->pyr_built)
        return 1;
    if (pyr_has_full(image))
        return image->pyr.count;
    return image->pyr.count + 1;
}

BOOL CmpImage_Level(cmp_image_t *image, int k, cmp_level_t *out)
{
    const view_level_t *level;
    if (!image || !out || k < 0 || !image->img.valid)
        return FALSE;
    if (k == 0) {
        out->px = image->img.px;
        out->w = image->img.w;
        out->h = image->img.h;
        out->pitch = image->img.pitch;
        out->shift = 0;
        return out->px != NULL;
    }
    pyr_build(image);
    if (!image->pyr_built)
        return FALSE;
    if (pyr_has_full(image)) {
        if (k >= image->pyr.count)
            return FALSE;
        level = &image->pyr.levels[k];
    } else {
        if (k - 1 >= image->pyr.count)
            return FALSE;
        level = &image->pyr.levels[k - 1];
    }
    if (!level->px || level->w <= 0 || level->h <= 0)
        return FALSE;
    out->px = level->px;
    out->w = level->w;
    out->h = level->h;
    out->pitch = level->pitch;
    out->shift = k;
    return TRUE;
}
