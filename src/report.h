#ifndef ROI_REPORT_H
#define ROI_REPORT_H

#include <windows.h>
#include "metrics.h"

BOOL Report_WriteHtmlFile(const metrics_item_result_t *items, int count,
                          const char *title, const char *file_path);
BOOL Report_GenerateAndOpen(const metrics_item_result_t *items, int count,
                            const char *title, const char *out_html_path);
char *Report_Base64Encode(const BYTE *data, size_t input_len,
                          size_t *out_len);
char *Report_AnsiToUtf8(const char *ansi_str);

#endif
