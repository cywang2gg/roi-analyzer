#ifndef ROI_LOG_H
#define ROI_LOG_H

#include <stddef.h>
#include "analyze.h"

int LogROI(const char *img_path, const char *mode, const roi_result_t *r);
int Log_Clear(const char *img_path, const char *mode);
void Log_GetPath(const char *img_path, const char *mode, char *dst, size_t cap);

#endif