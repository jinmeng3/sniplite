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
#include "../third_party/quirc/lib/quirc.h"

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

static int decode_gray(const uint8_t *pixels, int width, int height)
{
    struct quirc *q = quirc_new();
    if (!q) {
        fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
        return 1;
    }
    if (quirc_resize(q, width, height) < 0) {
        quirc_destroy(q);
        fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
        return 1;
    }
    int w, h;
    uint8_t *gray = quirc_begin(q, &w, &h);
    for (int y = 0; y < height; y++)
        memcpy(gray + (size_t)y * w, pixels + (size_t)y * width, width);
    quirc_end(q);

    int found = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    for (int i = 0; i < quirc_count(q); i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
            continue;
        if (found++) {
            static const char sep[] = "\r\n";
            DWORD wrote;
            WriteFile(out, sep, sizeof(sep) - 1, &wrote, NULL);
        }
        DWORD wrote;
        if (data.payload_len)
            WriteFile(out, data.payload, (DWORD)data.payload_len, &wrote, NULL);
    }
    if (found) {
        static const char end[] = "\r\n";
        DWORD wrote;
        WriteFile(out, end, sizeof(end) - 1, &wrote, NULL);
    }
    quirc_destroy(q);
    return found ? 0 : 4;
}

static int decode_scaled(const uint8_t *gray, UINT width, UINT height)
{
    size_t count = (size_t)width * height;
    int rc = decode_gray(gray, (int)width, (int)height);
    for (int scale = 2; rc == 4 && scale <= 4; scale++) {
        uint64_t scaled_count = count * (uint64_t)scale * scale;
        if (scaled_count > MAX_PIXELS || width > (UINT)(INT_MAX / scale) ||
                height > (UINT)(INT_MAX / scale))
            continue;
        int sw = (int)width * scale, sh = (int)height * scale;
        uint8_t *scaled = malloc((size_t)scaled_count);
        if (!scaled) {
            fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
            return 1;
        }
        for (UINT y = 0; y < height; y++) {
            for (int dy = 0; dy < scale; dy++) {
                uint8_t *row = scaled + ((size_t)y * scale + dy) * sw;
                const uint8_t *src = gray + (size_t)y * width;
                for (UINT x = 0; x < width; x++)
                    memset(row + (size_t)x * scale, src[x], scale);
            }
        }
        rc = decode_gray(scaled, sw, sh);
        free(scaled);
    }
    return rc;
}

static int decode_image(const Image *im)
{
    size_t count = (size_t)im->width * im->height;
    uint8_t *gray = malloc(count);
    if (!gray) {
        fwprintf(stderr, L"QR decoder could not allocate image buffers.\n");
        return 1;
    }
    for (UINT y = 0; y < im->height; y++) {
        for (UINT x = 0; x < im->width; x++) {
            uint32_t p = im->pixels[(size_t)y * im->width + x];
            unsigned b = p & 255, g = (p >> 8) & 255, r = (p >> 16) & 255;
            gray[(size_t)y * im->width + x] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
        }
    }
    int rc = decode_scaled(gray, im->width, im->height);
    free(gray);
    if (rc == 4)
        fwprintf(stderr, L"No readable QR code found.\n");
    return rc;
}

int wmain(int argc, WCHAR **argv)
{
    bool raw = argc == 4 && !wcscmp(argv[1], L"--raw");
    if (!raw && argc != 2) {
        fwprintf(stderr, L"Usage: sniplite_qr.exe <image-file> | --raw <width> <height>\n");
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
