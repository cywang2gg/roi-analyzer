#define COBJMACROS

#include "image.h"
#include "image_wic.h"

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
    lstrcpynA(img->path, path, MAX_PATH);
    lstrcpynA(img->decoder, "WIC", (int)sizeof(img->decoder));
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

BOOL Image_EncodePNGMemory(const BYTE *bgra_pixels, int width, int height,
                           int stride, BYTE **out_png_data,
                           size_t *out_png_size)
{
    IWICImagingFactory *factory = NULL;
    IStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *options = NULL;
    STATSTG stat;
    BYTE *png_data = NULL;
    size_t bytes, offset = 0;
    HRESULT hr;
    BOOL success = FALSE;

    if (out_png_data)
        *out_png_data = NULL;
    if (out_png_size)
        *out_png_size = 0;
    if (!bgra_pixels || !out_png_data || !out_png_size ||
        width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        stride < width * 4 ||
        (size_t)stride > SIZE_MAX / (size_t)height)
        return FALSE;
    bytes = (size_t)stride * (size_t)height;
    if (bytes > MAXDWORD)
        return FALSE;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                          CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void **)&factory);
    if (FAILED(hr))
        goto done;
    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
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
    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &options);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_Initialize(frame, options);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_SetSize(frame, (UINT)width, (UINT)height);
    if (FAILED(hr))
        goto done;
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
        if (FAILED(hr) || !IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA))
            goto done;
    }
    hr = IWICBitmapFrameEncode_WritePixels(frame, (UINT)height,
                                           (UINT)stride, (UINT)bytes,
                                           (BYTE *)bgra_pixels);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_Commit(frame);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapEncoder_Commit(encoder);
    if (FAILED(hr))
        goto done;
    hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
    if (FAILED(hr) || stat.cbSize.QuadPart == 0 ||
        stat.cbSize.QuadPart > (ULONGLONG)SIZE_MAX)
        goto done;
    png_data = (BYTE *)malloc((size_t)stat.cbSize.QuadPart);
    if (!png_data)
        goto done;
    {
        LARGE_INTEGER origin;
        origin.QuadPart = 0;
        hr = IStream_Seek(stream, origin, STREAM_SEEK_SET, NULL);
    }
    if (FAILED(hr))
        goto done;
    while (offset < (size_t)stat.cbSize.QuadPart) {
        ULONG chunk = (ULONG)(((size_t)stat.cbSize.QuadPart - offset) > MAXDWORD ?
                              MAXDWORD : (size_t)stat.cbSize.QuadPart - offset);
        ULONG read = 0;
        hr = IStream_Read(stream, png_data + offset, chunk, &read);
        if (FAILED(hr) || read != chunk)
            goto done;
        offset += read;
    }
    *out_png_data = png_data;
    *out_png_size = offset;
    png_data = NULL;
    success = TRUE;

done:
    free(png_data);
    if (options)
        IPropertyBag2_Release(options);
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (stream)
        IStream_Release(stream);
    if (factory)
        IWICImagingFactory_Release(factory);
    return success;
}

void Image_FreePNGMemory(BYTE *png_data)
{
    free(png_data);
}
