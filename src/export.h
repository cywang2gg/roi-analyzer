#ifndef ROI_EXPORT_H
#define ROI_EXPORT_H

#include <stddef.h>

#include "image.h"
#include "roi.h"

int Export_GetPath(const image_t *img, roi_mode_t mode, char *dst, size_t cap);
int Export_Log(const image_t *img, const roi_list_t *rois);

#endif
