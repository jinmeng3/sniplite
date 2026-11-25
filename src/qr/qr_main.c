#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../third_party/zxing-cpp/core/src/ZXingC.h"
#include "pixel_ops.h"

#define MAX_PIXELS (16u * 1024u * 1024u)

typedef struct {
    uint32_t *pixels;
    UINT width, height;
} Image;

static void image_free(Image *im)
{
    free(im->pixels);
    memset(im, 0, sizeof(*im));
}

static HRESULT image_load(const WCHAR *path, Image *im)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame,
                    &GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0,
                    WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = IWICBitmapSource_GetSize((IWICBitmapSource *)converter, &im->width, &im->height);
    if (SUCCEEDED(hr) && (!im->width || !im->height ||
            (uint64_t)im->width * im->height > MAX_PIXELS || im->width > INT_MAX / 4))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        im->pixels = malloc((size_t)im->width * im->height * sizeof(*im->pixels));
        if (!im->pixels)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = IWICBitmapSource_CopyPixels((IWICBitmapSource *)converter, NULL,
                    im->width * 4, im->width * im->height * 4, (BYTE *)im->pixels);
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (FAILED(hr))
        image_free(im);
    return hr;
}

static HRESULT image_read_raw(UINT width, UINT height, Image *im)
{
    if (!width || !height || (uint64_t)width * height > MAX_PIXELS || width > INT_MAX / 4)
        return E_INVALIDARG;
    im->width = width;
    im->height = height;
    size_t bytes = (size_t)width * height * sizeof(*im->pixels);
    im->pixels = malloc(bytes);
    if (!im->pixels)
        return E_OUTOFMEMORY;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    size_t at = 0;
    while (at < bytes) {
        size_t left = bytes - at;
        DWORD want = (DWORD)(left < (1u << 20) ? left : (1u << 20)), got = 0;
        if (!ReadFile(in, (BYTE *)im->pixels + at, want, &got, NULL) || !got) {
            image_free(im);
            return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        }
        at += got;
    }
    return S_OK;
}

static int decode_image(const Image *im)
{
    size_t count = (size_t)im->width * im->height;
    uint8_t *gray = malloc(count);
    if (!gray) {
        fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
        return 1;
    }
    pixel_bgra_to_gray(gray, (const unsigned char *)im->pixels, count);

    ZXing_ImageView *view = ZXing_ImageView_new_checked(gray, (int)count,
            (int)im->width, (int)im->height, ZXing_ImageFormat_Lum, (int)im->width, 1);
    ZXing_ReaderOptions *options = ZXing_ReaderOptions_new();
    if (!view || !options) {
        if (view) ZXing_ImageView_delete(view);
        if (options) ZXing_ReaderOptions_delete(options);
        free(gray);
        fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
        return 1;
    }

    const ZXing_BarcodeFormat format = ZXing_BarcodeFormat_QRCode;
    ZXing_ReaderOptions_setFormats(options, &format, 1);
    ZXing_ReaderOptions_setTryHarder(options, true);
    ZXing_ReaderOptions_setTryRotate(options, true);
    ZXing_ReaderOptions_setTryInvert(options, true);
    ZXing_ReaderOptions_setTryDownscale(options, true);
    ZXing_ReaderOptions_setMaxNumberOfSymbols(options, 32);
    ZXing_Barcodes *barcodes = ZXing_ReadBarcodes(view, options);
    ZXing_ReaderOptions_delete(options);
    ZXing_ImageView_delete(view);
    free(gray);
    if (!barcodes) {
        char *message = ZXing_LastErrorMsg();
        if (message) {
            fprintf(stderr, "ZXing error: %s\n", message);
            ZXing_free(message);
        } else {
            fwprintf(stderr, L"QR decoder failed.\n");
        }
        return 1;
    }

    int found = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    for (int i = 0; i < ZXing_Barcodes_size(barcodes); i++) {
        const ZXing_Barcode *barcode = ZXing_Barcodes_at(barcodes, i);
        if (!ZXing_Barcode_isValid(barcode))
            continue;
        char *text = ZXing_Barcode_text(barcode);
        if (!text)
            continue;
        if (found++) {
            static const char sep[] = "\r\n";
            DWORD wrote;
            WriteFile(out, sep, sizeof(sep) - 1, &wrote, NULL);
        }
        DWORD wrote;
        size_t length = strlen(text);
        if (length > 0)
            WriteFile(out, text, (DWORD)length, &wrote, NULL);
        ZXing_free(text);
    }
    if (found) {
        static const char end[] = "\r\n";
        DWORD wrote;
        WriteFile(out, end, sizeof(end) - 1, &wrote, NULL);
    }
    ZXing_Barcodes_delete(barcodes);
    if (!found)
        fwprintf(stderr, L"No readable QR code found.\n");
    return found ? 0 : 4;
}

int wmain(int argc, WCHAR **argv)
{
    bool raw = argc == 4 && !wcscmp(argv[1], L"--raw");
    if (!raw && argc != 2) {
        fwprintf(stderr, L"Usage: SnapRail_qr.exe <image-file> | --raw <width> <height>\n");
        return 2;
    }
    if (raw) {
        WCHAR *endw = NULL, *endh = NULL;
        unsigned long width = wcstoul(argv[2], &endw, 10), height = wcstoul(argv[3], &endh, 10);
        if (!*argv[2] || *endw || !*argv[3] || *endh || width > UINT_MAX || height > UINT_MAX)
            return 2;
        Image im = {0};
        HRESULT hr = image_read_raw((UINT)width, (UINT)height, &im);
        if (FAILED(hr)) {
            fwprintf(stderr, L"Could not read pixel input (0x%08lx).\n", (unsigned long)hr);
            return 1;
        }
        int rc = decode_image(&im);
        image_free(&im);
        return rc;
    }
    DWORD mode;
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode))
        SetConsoleOutputCP(CP_UTF8);
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fwprintf(stderr, L"Could not initialize Windows Imaging Component (0x%08lx).\n", (unsigned long)hr);
        return 1;
    }
    Image im = {0};
    hr = image_load(argv[1], &im);
    int rc;
    if (FAILED(hr)) {
        fwprintf(stderr, L"Could not read image (0x%08lx): %ls\n", (unsigned long)hr, argv[1]);
        rc = 1;
    } else {
        rc = decode_image(&im);
    }
    image_free(&im);
    CoUninitialize();
    return rc;
}
