#ifndef ROI_LOG_H
#define ROI_LOG_H

#include "analyze.h"

// Append one ROI to bin/roi_log.csv (19 cols, header auto-created) + bin/roi_log.txt.
// mode: "drag" / "fix3" / "fix5". Returns 0 on success.
int LogROI(const char *img_path, const char *mode, const roi_result_t *r);

// Delete bin/roi_log.csv + bin/roi_log.txt. Returns 0 on success.
int Log_Clear(void);

#endif
