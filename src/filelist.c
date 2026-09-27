#include "filelist.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shlwapi.h>

static filelist_t *g_sort_list;

static void copy_path(char destination[MAX_PATH], const char *source)
{
    size_t length = strlen(source);
    if (length >= MAX_PATH)
        length = MAX_PATH - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static int compare_indices(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return StrCmpLogicalW(g_sort_list->wide_names[ia],
                         g_sort_list->wide_names[ib]);
}

static BOOL ensure_capacity(filelist_t *list, int needed)
{
    int cap;
    char (*names)[MAX_PATH];
    wchar_t (*wide_names)[MAX_PATH];
    BOOL *acp_names;
    BOOL *cloud;

    if (needed <= list->cap)
        return TRUE;
    cap = list->cap ? list->cap : 32;
    while (cap < needed) {
        if (cap > INT_MAX / 2)
            return FALSE;
        cap *= 2;
    }
    names = (char (*)[MAX_PATH])malloc((size_t)cap * sizeof(*names));
    wide_names = (wchar_t (*)[MAX_PATH])malloc((size_t)cap * sizeof(*wide_names));
    acp_names = (BOOL *)malloc((size_t)cap * sizeof(*acp_names));
    cloud = (BOOL *)malloc((size_t)cap * sizeof(*cloud));
    if (!names || !wide_names || !acp_names || !cloud) {
        free(names);
        free(wide_names);
        free(acp_names);
        free(cloud);
        return FALSE;
    }
    if (list->count) {
        memcpy(names, list->names, (size_t)list->count * sizeof(*names));
        memcpy(wide_names, list->wide_names,
               (size_t)list->count * sizeof(*wide_names));
        memcpy(acp_names, list->acp_names,
               (size_t)list->count * sizeof(*acp_names));
        memcpy(cloud, list->cloud,
               (size_t)list->count * sizeof(*cloud));
    }
    free(list->names);
    free(list->wide_names);
    free(list->acp_names);
    free(list->cloud);
    list->names = names;
    list->wide_names = wide_names;
    list->acp_names = acp_names;
    list->cloud = cloud;
    list->cap = cap;
    return TRUE;
}

static void get_directory(const char *path, char dir[MAX_PATH])
{
    const char *slash;
    size_t length;

    copy_path(dir, path);
    slash = strrchr(dir, '\\');
    if (!slash)
        slash = strrchr(dir, '/');
    if (!slash) {
        strcpy(dir, ".\\");
        return;
    }
    length = (size_t)(slash - dir) + 1;
    dir[length] = '\0';
}

static BOOL append_file(filelist_t *list, const WIN32_FIND_DATAW *data)
{
    BOOL used_default = FALSE;
    int length;
    int index;

    if (!ensure_capacity(list, list->count + 1))
        return FALSE;
    index = list->count;
    {
        size_t wide_length = wcslen(data->cFileName);
        if (wide_length >= MAX_PATH)
            wide_length = MAX_PATH - 1;
        memcpy(list->wide_names[index], data->cFileName,
               wide_length * sizeof(wchar_t));
        list->wide_names[index][wide_length] = L'\0';
    }
    length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                 data->cFileName, -1, list->names[index],
                                 MAX_PATH, NULL, &used_default);
    list->acp_names[index] = length > 0 && !used_default;
    list->cloud[index] =
        (data->dwFileAttributes &
         (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
          FILE_ATTRIBUTE_RECALL_ON_OPEN |
          FILE_ATTRIBUTE_OFFLINE)) != 0;
    if (!list->acp_names[index])
        list->names[index][0] = '\0';
    list->count++;
    return TRUE;
}

static BOOL is_image_file(const wchar_t *name)
{
    const wchar_t *extension = PathFindExtensionW(name);
    return _wcsicmp(extension, L".png") == 0 ||
           _wcsicmp(extension, L".jpg") == 0 ||
           _wcsicmp(extension, L".jpeg") == 0 ||
           _wcsicmp(extension, L".bmp") == 0;
}

static BOOL sort_files(filelist_t *list)
{
    int *order;
    char (*names)[MAX_PATH];
    wchar_t (*wide_names)[MAX_PATH];
    BOOL *acp_names;
    BOOL *cloud;
    int i;

    if (list->count < 2)
        return TRUE;
    order = (int *)malloc((size_t)list->count * sizeof(*order));
    names = (char (*)[MAX_PATH])malloc((size_t)list->count * sizeof(*names));
    wide_names = (wchar_t (*)[MAX_PATH])malloc(
        (size_t)list->count * sizeof(*wide_names));
    acp_names = (BOOL *)malloc((size_t)list->count * sizeof(*acp_names));
    cloud = (BOOL *)malloc((size_t)list->count * sizeof(*cloud));
    if (!order || !names || !wide_names || !acp_names || !cloud) {
        free(order);
        free(names);
        free(wide_names);
        free(acp_names);
        free(cloud);
        return FALSE;
    }
    for (i = 0; i < list->count; i++)
        order[i] = i;
    g_sort_list = list;
    qsort(order, (size_t)list->count, sizeof(*order), compare_indices);
    g_sort_list = NULL;
    for (i = 0; i < list->count; i++) {
        int source = order[i];
        memcpy(names[i], list->names[source], sizeof(names[i]));
        memcpy(wide_names[i], list->wide_names[source], sizeof(wide_names[i]));
        acp_names[i] = list->acp_names[source];
        cloud[i] = list->cloud[source];
    }
    memcpy(list->names, names, (size_t)list->count * sizeof(*names));
    memcpy(list->wide_names, wide_names, (size_t)list->count * sizeof(*wide_names));
    memcpy(list->acp_names, acp_names, (size_t)list->count * sizeof(*acp_names));
    memcpy(list->cloud, cloud, (size_t)list->count * sizeof(*cloud));
    free(order);
    free(names);
    free(wide_names);
    free(acp_names);
    free(cloud);
    return TRUE;
}

BOOL FileList_Scan(filelist_t *list, const char *dir)
{
    filelist_t next;
    WIN32_FIND_DATAW data;
    wchar_t wide_dir[MAX_PATH], pattern[MAX_PATH];
    HANDLE find;
    BOOL ok = TRUE;
    size_t length;

    if (!list || !dir || !dir[0])
        return FALSE;
    ZeroMemory(&next, sizeof(next));
    copy_path(next.dir, dir);
    length = strlen(next.dir);
    if (length && next.dir[length - 1] != '\\' && next.dir[length - 1] != '/') {
        if (length + 1 >= MAX_PATH)
            return FALSE;
        next.dir[length++] = '\\';
        next.dir[length] = '\0';
    }
    if (MultiByteToWideChar(CP_ACP, 0, next.dir, -1, wide_dir,
                            MAX_PATH) <= 0)
        return FALSE;
    if (_snwprintf(pattern, MAX_PATH, L"%ls*", wide_dir) < 0) {
        return FALSE;
    }
    pattern[MAX_PATH - 1] = L'\0';
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                          FILE_ATTRIBUTE_HIDDEN)) ||
                !is_image_file(data.cFileName))
                continue;
            if (!append_file(&next, &data)) {
                ok = FALSE;
                break;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND &&
               GetLastError() != ERROR_NO_MORE_FILES) {
        ok = FALSE;
    }
    if (!ok) {
        FileList_Free(&next);
        return FALSE;
    }
    if (!sort_files(&next)) {
        FileList_Free(&next);
        return FALSE;
    }
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetFileAttributesExA(next.dir, GetFileExInfoStandard, &attributes))
            next.dir_mtime = attributes.ftLastWriteTime;
    }
    for (length = 0; length < (size_t)next.count; length++)
        if (!next.acp_names[length])
            next.non_acp_count++;
    next.scanned = TRUE;
    FileList_Free(list);
    *list = next;
    return TRUE;
}

BOOL FileList_Refresh(filelist_t *list, const char *image_path)
{
    char dir[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    FILETIME mtime;

    if (!list || !image_path || !image_path[0])
        return FALSE;
    get_directory(image_path, dir);
    if (GetFileAttributesExA(dir, GetFileExInfoStandard, &attributes))
        mtime = attributes.ftLastWriteTime;
    else
        ZeroMemory(&mtime, sizeof(mtime));
    if (list->scanned && _stricmp(list->dir, dir) == 0 &&
        CompareFileTime(&list->dir_mtime, &mtime) == 0)
        return TRUE;
    return FileList_Scan(list, dir);
}

int FileList_Find(filelist_t *list, const char *name, BOOL *exact)
{
    int i, low, high;
    wchar_t wide_name[MAX_PATH];
    if (exact)
        *exact = FALSE;
    if (!list || !name)
        return -1;
    for (i = 0; i < list->count; i++) {
        if (list->acp_names[i] && _stricmp(list->names[i], name) == 0) {
            if (exact)
                *exact = TRUE;
            return i;
        }
    }
    if (list->scanned) {
        if (!FileList_Scan(list, list->dir))
            return -1;
        for (i = 0; i < list->count; i++) {
            if (list->acp_names[i] && _stricmp(list->names[i], name) == 0) {
                if (exact)
                    *exact = TRUE;
                return i;
            }
        }
    }
    if (MultiByteToWideChar(CP_ACP, 0, name, -1, wide_name, MAX_PATH) <= 0)
        return list->count;
    low = 0;
    high = list->count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (StrCmpLogicalW(list->wide_names[middle], wide_name) < 0)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

BOOL FileList_Path(const filelist_t *list, int index, char path[MAX_PATH])
{
    size_t dir_length, name_length;
    if (!list || !path || index < 0 || index >= list->count ||
        !list->acp_names[index])
        return FALSE;
    dir_length = strlen(list->dir);
    name_length = strlen(list->names[index]);
    if (dir_length + name_length >= MAX_PATH)
        return FALSE;
    memcpy(path, list->dir, dir_length);
    memcpy(path + dir_length, list->names[index], name_length + 1);
    return TRUE;
}

void FileList_Free(filelist_t *list)
{
    if (!list)
        return;
    free(list->names);
    free(list->wide_names);
    free(list->acp_names);
    free(list->cloud);
    ZeroMemory(list, sizeof(*list));
}
