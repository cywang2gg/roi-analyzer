#ifndef ROI_RENAME_H
#define ROI_RENAME_H

#include <windows.h>
#include <stddef.h>

BOOL ExtractPrefix(const char *new_name, const char *orig_name,
                   char *prefix, size_t cap);
BOOL Rename_ValidateFileName(const char *file_name, char *err_msg,
                             size_t err_cap);
BOOL Rename_IsSupportedExtension(const char *file_path);
DWORD Rename_Execute(const char *old_path, const char *new_name,
                     char *out_new_path, size_t cap, BOOL overwrite);

#endif
