#ifndef ROI_FILELIST_H
#define ROI_FILELIST_H

#include <windows.h>

typedef struct {
    char (*names)[MAX_PATH];
    wchar_t (*wide_names)[MAX_PATH];
    BOOL *acp_names;
    BOOL *cloud;
    int count;
    int cap;
    char dir[MAX_PATH];
    FILETIME dir_mtime;
    BOOL scanned;
    int non_acp_count;
} filelist_t;

BOOL FileList_Scan(filelist_t *list, const char *dir);
BOOL FileList_Refresh(filelist_t *list, const char *image_path);
int FileList_Find(filelist_t *list, const char *name, BOOL *exact);
BOOL FileList_Path(const filelist_t *list, int index, char path[MAX_PATH]);
void FileList_Free(filelist_t *list);

#endif
