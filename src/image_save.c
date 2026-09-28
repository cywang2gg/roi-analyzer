#define COBJMACROS

#include "image_save.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

static BOOL encode_png(const image_t *img, const char *path)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *properties = NULL;
    IStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    ULARGE_INTEGER size;
    HRESULT hr;
    BOOL ok = FALSE;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                          CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void **)&factory);
    if (FAILED(hr))
        goto done;
    hr = IWICImagingFactory_CreateBitmapFromMemory(
        factory, (UINT)img->w, (UINT)img->h, &GUID_WICPixelFormat32bppBGRA,
        (UINT)img->pitch, (UINT)((size_t)img->pitch * (size_t)img->h),
        img->px, &source);
    if (FAILED(hr))
        goto done;
    hr = SHCreateStreamOnFileA(path, STGM_WRITE | STGM_SHARE_DENY_WRITE,
                               &stream);
    if (FAILED(hr))
        goto done;
    size.QuadPart = 0;
    hr = IStream_SetSize(stream, size);
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
    hr = IWICBitmapFrameEncode_SetSize(frame, (UINT)img->w, (UINT)img->h);
    if (FAILED(hr))
        goto done;
    hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (FAILED(hr) || !IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA))
        goto done;
    hr = IWICBitmapFrameEncode_WriteSource(
        frame, (IWICBitmapSource *)source, NULL);
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

int Image_SavePNG(const image_t *img, const char *path)
{
    char directory[MAX_PATH], temporary[MAX_PATH];
    DWORD attributes;
    size_t bytes;

    if (!img || !img->valid || !img->px || img->w <= 0 || img->h <= 0 ||
        img->w > INT_MAX / 4 || img->pitch < img->w * 4 ||
        (size_t)img->pitch > SIZE_MAX / (size_t)img->h)
        return -1;
    bytes = (size_t)img->pitch * (size_t)img->h;
    if (!path || !path[0] || (size_t)img->pitch > UINT_MAX ||
        bytes > UINT_MAX)
        return -1;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;

    lstrcpynA(directory, path, MAX_PATH);
    if (!PathRemoveFileSpecA(directory))
        lstrcpynA(directory, ".", MAX_PATH);
    if (!GetTempFileNameA(directory, "roi", 0, temporary))
        return -1;
    if (!encode_png(img, temporary)) {
        DeleteFileA(temporary);
        return -1;
    }
    if (!MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary);
        return -1;
    }
    return 0;
}
