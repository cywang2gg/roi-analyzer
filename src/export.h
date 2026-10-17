#ifndef ROI_EXPORT_H
#define ROI_EXPORT_H

#include <stddef.h>

#include "image.h"
#include "pvt_locate.h"
#include "roi.h"

int Export_GetPath(const image_t *img, roi_mode_t mode, char *dst, size_t cap);
int Export_Log(const image_t *img, const roi_list_t *rois,
               char *error_path, size_t error_path_cap);
int Export_PvtLog(const char *image_path, const pvt_target_t *targets,
                  size_t count);

#endif
