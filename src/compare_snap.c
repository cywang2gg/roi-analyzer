#define COBJMACROS

#include "compare.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

typedef HANDLE (WINAPI *snap_set_thread_dpi_context_fn)(HANDLE);

int Snap_Round(double value)
{
    if (!isfinite(value) || value > INT_MAX || value < INT_MIN)
        return 0;
    return (int)(value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5));
}

double Snap_PhysicalScale(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    snap_set_thread_dpi_context_fn set_context;
    FARPROC procedure;
    RECT logical_rect, physical_rect;
    HANDLE previous;
    double scale = 1.0;

    if (!hwnd || !user32 ||
        !GetWindowRect(hwnd, &logical_rect) ||
        logical_rect.right <= logical_rect.left)
        return scale;
    procedure = GetProcAddress(user32, "SetThreadDpiAwarenessContext");
    if (!procedure || sizeof(set_context) != sizeof(procedure))
        return scale;
    memcpy(&set_context, &procedure, sizeof(set_context));
    previous = set_context((HANDLE)(LONG_PTR)-4);
    if (!previous)
        return scale;
    if (GetWindowRect(hwnd, &physical_rect) &&
        physical_rect.right > physical_rect.left) {
        double candidate = (double)(physical_rect.right - physical_rect.left) /
                           (double)(logical_rect.right - logical_rect.left);
        if (isfinite(candidate) && candidate > 0.0)
            scale = candidate;
    }
    set_context(previous);
    return scale;
}

HFONT CmpSnap_CreateScaledFont(double scale)
{
    LOGFONTA source;
    int dpi;
    if (!isfinite(scale) || scale <= 0.0)
        return NULL;
    if (GetObjectA(Cmp_UiFont(), (int)sizeof(source), &source) !=
        (int)sizeof(source))
        return NULL;
    dpi = Snap_Round(scale * 96.0);
    if (dpi <= 0)
        return NULL;
    return CreateFontA(MulDiv(source.lfHeight, dpi, 96),
                       source.lfWidth, source.lfEscapement,
                       source.lfOrientation, source.lfWeight,
                       source.lfItalic, source.lfUnderline,
                       source.lfStrikeOut, source.lfCharSet,
                       source.lfOutPrecision, source.lfClipPrecision,
                       source.lfQuality, source.lfPitchAndFamily,
                       source.lfFaceName);
}

void CmpInfo_Add(char lines[CMP_MAX_CELLS + 1][256], int *count,
                 const char *text)
{
    char *out;
    const char *in;
    size_t used = 0;
    if (!lines || !count || !text || *count < 0 ||
        *count >= CMP_MAX_CELLS + 1)
        return;
    out = lines[*count];
    for (in = text; *in && used < 254; ) {
        const char *next = CharNextA(in);
        size_t bytes = (size_t)(next - in);
        if (bytes > 254 - used)
            break;
        memcpy(out + used, in, bytes);
        used += bytes;
        in = next;
    }
    out[used] = '\0';
    (*count)++;
}

int CmpInfo_Height(HFONT font, int lines)
{
    HDC dc;
    HGDIOBJ old_font;
    TEXTMETRICA metrics;
    int line_height = 16;
    if (lines <= 0)
        return 0;
    dc = GetDC(NULL);
    if (dc) {
        old_font = SelectObject(dc, font ? font : Cmp_UiFont());
        if (GetTextMetricsA(dc, &metrics))
            line_height = metrics.tmHeight;
        if (old_font)
            SelectObject(dc, old_font);
        ReleaseDC(NULL, dc);
    }
    return (line_height + 4) * lines + 8;
}

void CmpInfo_Draw(HDC dc, int width, int top, HFONT font,
                  char lines[CMP_MAX_CELLS + 1][256], int count)
{
    HGDIOBJ old_font;
    RECT band;
    int i, height;
    if (!dc || width <= 0 || !lines || count <= 0)
        return;
    height = CmpInfo_Height(font, count);
    band.left = 0;
    band.top = top;
    band.right = width;
    band.bottom = top + height;
    FillRect(dc, &band, (HBRUSH)GetStockObject(BLACK_BRUSH));
    old_font = SelectObject(dc, font ? font : Cmp_UiFont());
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 245, 245));
    for (i = 0; i < count; i++) {
        RECT text = { 8, top + 4 + i * (height - 8) / count,
                      width - 8, top + 4 + (i + 1) * (height - 8) / count };
        DrawTextA(dc, lines[i], -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    }
    if (old_font)
        SelectObject(dc, old_font);
}

BOOL CmpSnap_Begin(HWND owner, int width, int height, cmp_snap_t *snapshot)
{
    HDC source;
    if (!snapshot || width <= 0 || height <= 0)
        return FALSE;
    ZeroMemory(snapshot, sizeof(*snapshot));
    source = GetDC(owner);
    if (!source)
        return FALSE;
    snapshot->dc = CreateCompatibleDC(source);
    snapshot->bitmap = CreateCompatibleBitmap(source, width, height);
    ReleaseDC(owner, source);
    if (!snapshot->dc || !snapshot->bitmap) {
        CmpSnap_End(snapshot);
        return FALSE;
    }
    snapshot->old_bitmap = (HBITMAP)SelectObject(snapshot->dc,
                                                 snapshot->bitmap);
    if (!snapshot->old_bitmap || snapshot->old_bitmap == (HBITMAP)HGDI_ERROR) {
        CmpSnap_End(snapshot);
        return FALSE;
    }
    snapshot->width = width;
    snapshot->height = height;
    return TRUE;
}

void CmpSnap_Finalize(cmp_snap_t *snapshot)
{
    if (!snapshot)
        return;
    if (snapshot->dc && snapshot->old_bitmap &&
        snapshot->old_bitmap != (HBITMAP)HGDI_ERROR)
        SelectObject(snapshot->dc, snapshot->old_bitmap);
    if (snapshot->dc)
        DeleteDC(snapshot->dc);
    snapshot->dc = NULL;
    snapshot->old_bitmap = NULL;
}

void CmpSnap_End(cmp_snap_t *snapshot)
{
    if (!snapshot)
        return;
    CmpSnap_Finalize(snapshot);
    if (snapshot->bitmap)
        DeleteObject(snapshot->bitmap);
    ZeroMemory(snapshot, sizeof(*snapshot));
}

static BOOL snap_directory(char directory[MAX_PATH])
{
    char profile[MAX_PATH];
    DWORD length = GetEnvironmentVariableA("USERPROFILE", profile,
                                            MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        char pictures[MAX_PATH];
        if (!PathCombineA(pictures, profile, "Pictures"))
            return FALSE;
        if (GetFileAttributesA(pictures) == INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryA(pictures, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            return FALSE;
        if (!PathCombineA(directory, pictures, "ROI Analyzer Snapshots"))
            return FALSE;
    } else {
        DWORD temp_length = GetTempPathA(MAX_PATH, directory);
        if (!temp_length || temp_length >= MAX_PATH ||
            !PathAppendA(directory, "ROI Analyzer Snapshots"))
            return FALSE;
    }
    if (GetFileAttributesA(directory) == INVALID_FILE_ATTRIBUTES &&
        !CreateDirectoryA(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;
    return TRUE;
}

static void snap_base_name(char output[33], const char *path)
{
    const char *name, *extension, *cursor;
    size_t used = 0;
    name = path && *path ? PathFindFileNameA(path) : "image";
    extension = PathFindExtensionA(name);
    for (cursor = name; *cursor && cursor < extension; ) {
        const char *next = CharNextA(cursor);
        size_t bytes = (size_t)(next - cursor);
        if (bytes > 32 - used)
            break;
        memcpy(output + used, cursor, bytes);
        used += bytes;
        cursor = next;
    }
    if (!used) {
        memcpy(output, "image", 5);
        used = 5;
    }
    output[used] = '\0';
}

static BOOL snap_ref_directory(const char *ref_path, char directory[MAX_PATH])
{
    DWORD attributes;
    if (!ref_path || !*ref_path || strlen(ref_path) >= MAX_PATH ||
        (!strchr(ref_path, '\\') && !strchr(ref_path, '/')))
        return FALSE;
    memcpy(directory, ref_path, strlen(ref_path) + 1);
    if (!PathRemoveFileSpecA(directory))
        return FALSE;
    attributes = GetFileAttributesA(directory);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

BOOL CmpSnap_MakePath(const SYSTEMTIME *time, const char *ref_path,
                      const char *name_a, const char *name_b,
                      char *path, size_t capacity)
{
    SYSTEMTIME now;
    char directory[MAX_PATH];
    char combined[MAX_PATH];
    char base_a[33], base_b[33];
    char filename[MAX_PATH];
    unsigned int suffix;
    int written;
    if (!path || capacity == 0 || capacity > INT_MAX)
        return FALSE;
    path[0] = '\0';
    if (!time) {
        GetLocalTime(&now);
        time = &now;
    }
    if (!snap_ref_directory(ref_path, directory) &&
        !snap_directory(directory))
        return FALSE;
    snap_base_name(base_a, name_a);
    snap_base_name(base_b, name_b);
    for (suffix = 0; suffix < 10000; suffix++) {
        if (suffix)
            written = _snprintf(filename, sizeof(filename),
                                "snap_%s_vs_%s_%04u%02u%02u-%02u%02u%02u_%u.png",
                                base_a, base_b,
                                (unsigned int)time->wYear,
                                (unsigned int)time->wMonth,
                                (unsigned int)time->wDay,
                                (unsigned int)time->wHour,
                                (unsigned int)time->wMinute,
                                (unsigned int)time->wSecond,
                                suffix);
        else
            written = _snprintf(filename, sizeof(filename),
                                "snap_%s_vs_%s_%04u%02u%02u-%02u%02u%02u.png",
                                base_a, base_b,
                                (unsigned int)time->wYear,
                                (unsigned int)time->wMonth,
                                (unsigned int)time->wDay,
                                (unsigned int)time->wHour,
                                (unsigned int)time->wMinute,
                                (unsigned int)time->wSecond);
        if (written < 0 || (size_t)written >= sizeof(filename) ||
            !PathCombineA(combined, directory, filename))
            return FALSE;
        if (strlen(combined) >= capacity)
            return FALSE;
        if (GetFileAttributesA(combined) == INVALID_FILE_ATTRIBUTES) {
            memcpy(path, combined, strlen(combined) + 1);
            return TRUE;
        }
    }
    path[0] = '\0';
    return FALSE;
}

static BOOL snap_encode(HBITMAP bitmap, const char *path)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *properties = NULL;
    IStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    ULARGE_INTEGER empty_size;
    HRESULT hr;
    BOOL ok = FALSE;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                          CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void **)&factory);
    if (FAILED(hr))
        goto done;
    hr = IWICImagingFactory_CreateBitmapFromHBITMAP(
        factory, bitmap, NULL, WICBitmapIgnoreAlpha, &source);
    if (FAILED(hr))
        goto done;
    hr = SHCreateStreamOnFileA(path, STGM_WRITE | STGM_SHARE_DENY_WRITE,
                               &stream);
    if (FAILED(hr))
        goto done;
    empty_size.QuadPart = 0;
    hr = IStream_SetSize(stream, empty_size);
    if (FAILED(hr))
        goto done;
    hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng,
                                           NULL, &encoder);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapEncoder_Initialize(encoder, stream,
                                      WICBitmapEncoderNoCache);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &properties);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_Initialize(frame, properties);
    if (FAILED(hr))
        goto done;
    {
        UINT width, height;
        hr = IWICBitmap_GetSize(source, &width, &height);
        if (FAILED(hr))
            goto done;
        hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
        if (FAILED(hr))
            goto done;
    }
    hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (FAILED(hr) || !IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA))
        goto done;
    hr = IWICBitmapFrameEncode_WriteSource(frame,
                                            (IWICBitmapSource *)source, NULL);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_Commit(frame);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapEncoder_Commit(encoder);
    ok = SUCCEEDED(hr);
done:
    if (properties)
        IPropertyBag2_Release(properties);
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (stream)
        IStream_Release(stream);
    if (source)
        IWICBitmap_Release(source);
    if (factory)
        IWICImagingFactory_Release(factory);
    return ok;
}

BOOL CmpSnap_SavePng(HBITMAP bitmap, char *path, size_t capacity,
                     BOOL overwrite)
{
    HANDLE file;
    size_t length;
    if (!path || capacity == 0 || capacity > INT_MAX)
        return FALSE;
    length = strlen(path);
    if (!bitmap || !length || length >= capacity) {
        path[0] = '\0';
        return FALSE;
    }
    if (overwrite) {
        char temporary[MAX_PATH];
        char directory[MAX_PATH];
        lstrcpynA(directory, path, MAX_PATH);
        if (!PathRemoveFileSpecA(directory) ||
            !GetTempFileNameA(directory, "roi", 0, temporary)) {
            path[0] = '\0';
            return FALSE;
        }
        if (!snap_encode(bitmap, temporary)) {
            DeleteFileA(temporary);
            path[0] = '\0';
            return FALSE;
        }
        if (!MoveFileExA(temporary, path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary);
            path[0] = '\0';
            return FALSE;
        }
        return TRUE;
    }
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        path[0] = '\0';
        return FALSE;
    }
    CloseHandle(file);
    if (snap_encode(bitmap, path))
        return TRUE;
    DeleteFileA(path);
    path[0] = '\0';
    return FALSE;
}

BOOL cmp_save_as_dialog(HWND owner, char *path, size_t capacity)
{
    OPENFILENAMEA ofn;
    char selected[MAX_PATH] = { 0 };
    char initial_directory[MAX_PATH] = { 0 };
    char pictures[MAX_PATH] = { 0 };
    const char *filename = path && *path ? PathFindFileNameA(path) : NULL;
    DWORD attributes;
    size_t length;
    if (!path || capacity == 0 || capacity > INT_MAX)
        return FALSE;
    if (filename && *filename)
        lstrcpynA(selected, filename, MAX_PATH);
    else
        lstrcpynA(selected, "snapshot.png", MAX_PATH);
    if (path[0] && strlen(path) < MAX_PATH) {
        lstrcpynA(initial_directory, path, MAX_PATH);
        if (PathRemoveFileSpecA(initial_directory)) {
            attributes = GetFileAttributesA(initial_directory);
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                initial_directory[0] = '\0';
        }
    }
    if (!initial_directory[0]) {
        if (!snap_directory(pictures))
            return FALSE;
        if (!PathRemoveFileSpecA(pictures))
            return FALSE;
        lstrcpynA(initial_directory, pictures, MAX_PATH);
    }
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "PNG image\0*.png\0";
    ofn.lpstrFile = selected;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initial_directory;
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&ofn))
        return FALSE;
    length = strlen(selected);
    if (length >= capacity) {
        path[0] = '\0';
        return FALSE;
    }
    memcpy(path, selected, length + 1);
    return TRUE;
}

BOOL CmpSnap_CopyToClipboard(HWND owner, HBITMAP bitmap)
{
    HBITMAP copy;
    int attempt;
    if (!bitmap)
        return FALSE;
    copy = (HBITMAP)CopyImage(bitmap, IMAGE_BITMAP, 0, 0,
                              LR_CREATEDIBSECTION);
    if (!copy)
        return FALSE;
    for (attempt = 0; attempt < 5; attempt++) {
        if (OpenClipboard(owner)) {
            BOOL ok = EmptyClipboard() &&
                      SetClipboardData(CF_BITMAP, copy) != NULL;
            CloseClipboard();
            if (ok)
                return TRUE;
            DeleteObject(copy);
            return FALSE;
        }
        if (attempt < 4)
            Sleep(20);
    }
    DeleteObject(copy);
    return FALSE;
}

BOOL CmpSnap_Deliver(HWND owner, HBITMAP bitmap, const SYSTEMTIME *time,
                     const char *ref_path, const char *name_a,
                     const char *name_b, char *path, size_t capacity)
{
    char suggested[MAX_PATH];
    BOOL copied, saved = FALSE;
    if (!path || capacity == 0 || capacity > INT_MAX)
        return FALSE;
    path[0] = '\0';
    copied = CmpSnap_CopyToClipboard(owner, bitmap);
    if (CmpSnap_MakePath(time, ref_path, name_a, name_b, path, capacity)) {
        lstrcpynA(suggested, path, MAX_PATH);
        saved = CmpSnap_SavePng(bitmap, path, capacity, FALSE);
    } else {
        suggested[0] = '\0';
    }
    if (!saved) {
        if (suggested[0])
            lstrcpynA(path, suggested, (int)capacity);
        else
            path[0] = '\0';
        if (cmp_save_as_dialog(owner, path, capacity))
            saved = CmpSnap_SavePng(bitmap, path, capacity, TRUE);
    }
    if (!saved)
        path[0] = '\0';
    return copied;
}
