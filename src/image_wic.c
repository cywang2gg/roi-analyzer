#define COBJMACROS

#include "image.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wincodec.h>

int Image_LoadWIC(image_t *img, const char *path)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER file_size;
    HGLOBAL memory = NULL;
    BYTE *file_data = NULL;
    IStream *stream = NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    UINT width = 0, height = 0;
    size_t rowbytes, bytes, offset = 0;
    HRESULT hr;
    int result = -1;
    DWORD read;

    if (!img || !path || !path[0])
        return -1;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        goto done;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
        (ULONGLONG)file_size.QuadPart > (ULONGLONG)SIZE_MAX)
        goto done;
    file_data = (BYTE *)malloc((size_t)file_size.QuadPart);
    if (!file_data)
        goto done;
    while (offset < (size_t)file_size.QuadPart) {
        DWORD chunk = (DWORD)(((size_t)file_size.QuadPart - offset) > MAXDWORD ?
                              MAXDWORD : (size_t)file_size.QuadPart - offset);
        if (!ReadFile(file, file_data + offset, chunk, &read, NULL) || !read)
            goto done;
        offset += read;
    }
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;

    memory = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)file_size.QuadPart);
    if (!memory)
        goto done;
    {
        void *locked = GlobalLock(memory);
        if (!locked)
            goto done;
        memcpy(locked, file_data, (size_t)file_size.QuadPart);
        GlobalUnlock(memory);
    }
    hr = CreateStreamOnHGlobal(memory, TRUE, &stream);
    if (FAILED(hr))
        goto done;
    memory = NULL;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr))
        goto done;
    hr = IWICImagingFactory_CreateDecoderFromStream(
        factory, stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    if (FAILED(hr) || !width || !height || width > 16384 || height > 16384)
        goto done;
    rowbytes = (size_t)width * 4;
    if (rowbytes / 4 != (size_t)width)
        goto done;
    bytes = rowbytes * (size_t)height;
    if (bytes / (size_t)height != rowbytes)
        goto done;
    if (rowbytes > INT_MAX)
        goto done;
    hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (FAILED(hr))
        goto done;
    hr = IWICFormatConverter_Initialize(
        converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        goto done;

    img->px = (unsigned char *)malloc(bytes);
    if (!img->px)
        goto done;
    hr = IWICFormatConverter_CopyPixels(converter, NULL, (UINT)rowbytes,
                                         (UINT)bytes, img->px);
    if (FAILED(hr)) {
        free(img->px);
        img->px = NULL;
        goto done;
    }
    img->w = (int)width;
    img->h = (int)height;
    img->pitch = (int)rowbytes;
    strncpy(img->path, path, MAX_PATH - 1);
    img->path[MAX_PATH - 1] = '\0';
    strcpy(img->decoder, "WIC");
    img->valid = TRUE;
    result = 0;

done:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (memory)
        GlobalFree(memory);
    free(file_data);
    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (factory)
        IWICImagingFactory_Release(factory);
    if (stream)
        IStream_Release(stream);
    return result;
}
