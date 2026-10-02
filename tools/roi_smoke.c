#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <gdiplus/gdiplus.h>

#include "analyze.h"
#include "export.h"
#include "image.h"
#include "roi.h"

static long file_size(const char *path)
{
    FILE *file;
    long size;

    file = fopen(path, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    if (fclose(file) != 0)
        return -1;
    return size;
}

static int count_wtext(const WCHAR *text, const WCHAR *needle)
{
    int count = 0;
    size_t length = wcslen(needle);
    const WCHAR *found = text;

    while ((found = wcsstr(found, needle)) != NULL) {
        count++;
        found += length;
    }
    return count;
}

static int verify_utf16le_append(const char *path, long start)
{
    static const unsigned char bom[] = { 0xFF, 0xFE };
    static const WCHAR header[] =
        L"id\tRm\tRs\tGm\tGs\tBm\tBs\tYm\tYs\tL\ta\tb\trect\tcount\r\n";
    FILE *file;
    unsigned char *bytes;
    WCHAR *output;
    WCHAR *section;
    WCHAR *line_start, *line_end;
    long end;
    size_t byte_count, char_count, i, section_index;
    int data_rows = 0, bad_tabs = 0;
    int failed;

    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    end = ftell(file);
    if (end < 2 || end < start || (end & 1) != 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    byte_count = (size_t)end;
    bytes = (unsigned char *)malloc(byte_count);
    if (bytes == NULL) {
        fclose(file);
        return -1;
    }
    if (fread(bytes, 1, byte_count, file) != byte_count)
        failed = 1;
    else
        failed = 0;
    if (fclose(file) != 0)
        failed = 1;
    if (failed || bytes[0] != bom[0] || bytes[1] != bom[1]) {
        free(bytes);
        return -1;
    }
    for (i = 2; i + 1 < byte_count; i += 2) {
        if (bytes[i] == bom[0] && bytes[i + 1] == bom[1]) {
            free(bytes);
            return -1;
        }
    }
    char_count = byte_count / 2 - 1;
    output = (WCHAR *)malloc((char_count + 1) * sizeof(*output));
    if (output == NULL) {
        free(bytes);
        return -1;
    }
    for (i = 0; i < char_count; i++)
        output[i] = (WCHAR)(bytes[(i + 1) * 2] |
                            ((unsigned int)bytes[(i + 1) * 2 + 1] << 8));
    output[char_count] = L'\0';
    free(bytes);

    section_index = start == 0 ? 0 : (size_t)(start / 2 - 1);
    if ((start & 1) != 0 || section_index > char_count ||
        (start > 0 && section_index + 2 > char_count)) {
        free(output);
        return -1;
    }
    section = output + section_index;
    if ((start > 0 && (section[0] != L'\r' || section[1] != L'\n')) ||
        count_wtext(section, L"# ==== export ") != 2 ||
        count_wtext(section, header) != 2 ||
        count_wtext(section, L"1\t") != 2 ||
        wcsstr(section, L"\r\n\r\n# ==== export ") == NULL) {
        free(output);
        return -1;
    }
    for (i = 0; i < char_count; i++) {
        if (output[i] == L'\n' &&
            (i == 0 || output[i - 1] != L'\r')) {
            free(output);
            return -1;
        }
    }
    line_start = section;
    while (*line_start) {
        int tabs = 0;
        line_end = wcschr(line_start, L'\n');
        if (!line_end)
            break;
        *line_end = L'\0';
        if (line_start[0] == L'1' && line_start[1] == L'\t' &&
            line_start[2] >= L'0' && line_start[2] <= L'9') {
            data_rows++;
            for (i = 0; line_start[i]; i++)
                if (line_start[i] == L'\t')
                    tabs++;
            if (tabs != 13)
                bad_tabs = 1;
        }
        line_start = line_end + 1;
    }
    free(output);
    if (data_rows != 2 || bad_tabs)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    GdiplusStartupInput startup = { 1, NULL, FALSE, FALSE };
    ULONG_PTR token = 0;
    image_t img;
    roi_item_t item;
    roi_list_t rois;
    RECT rect;
    char path[MAX_PATH];
    long start;

    if (argc < 2) {
        printf("usage: roi_smoke <img>\n");
        return 2;
    }
    memset(&img, 0, sizeof(img));
    memset(&item, 0, sizeof(item));
    memset(&rois, 0, sizeof(rois));
    if (GdiplusStartup(&token, &startup, NULL) != 0) {
        printf("gdiplus fail\n");
        return 1;
    }
    if (Image_Load(&img, argv[1]) != 0) {
        printf("LOAD FAIL %s\n", argv[1]);
        GdiplusShutdown(token);
        return 1;
    }
    rect.left = 0;
    rect.top = 0;
    rect.right = img.w - 1;
    rect.bottom = img.h - 1;
    item.rc = rect;
    item.source = ROI_SRC_MANUAL;
    AnalyzeROI(&img, rect, &item.res);
    rois.items = &item;
    rois.count = 1;
    rois.cap = 1;
    rois.selected = 0;
    if (Export_GetPath(&img, MODE_DRAG, path, sizeof(path)) != 0) {
        printf("EXPORT PATH FAIL\n");
        Image_Free(&img);
        GdiplusShutdown(token);
        return 1;
    }
    start = file_size(path);
    if (start < 0 || Export_Log(&img, &rois, NULL, 0) != 0 ||
        Export_Log(&img, &rois, NULL, 0) != 0 ||
        verify_utf16le_append(path, start) != 0) {
        printf("CSV EXPORT FAIL: %s\n", path);
        Image_Free(&img);
        GdiplusShutdown(token);
        return 1;
    }
    printf("csv=%s\nexport ok\n", path);
    Image_Free(&img);
    GdiplusShutdown(token);
    return 0;
}
