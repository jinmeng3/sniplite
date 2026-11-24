/* The optional text OCR module: CrispEmbed's PP-OCRv6 GGUF models and GGML runtime.
 * The release ZIP is built by package-all.bat. Upstream models/DLLs have pinned SHA-256;
 * the fixed native engine and its license must match this executable's resources. */
#include "ocr.h"
#include "update.h"
#include "formula.h"
#include "resource.h"

/* The published OCR zip. Its size is used for download progress. */
#define MODULE_TAG UPDATE_REPO L"/releases/download/v" SNIPLITE_VERSION L"/"
#define MODULE_PATH MODULE_TAG L"sniplite_ocr.zip"
#define MODULE_BYTES 10878329u
#define QR_MODULE_PATH UPDATE_REPO L"/releases/download/v" SNIPLITE_VERSION L"/sniplite_" SNIPLITE_VERSION L"_portable_QR_x64.zip"
#define QR_ARCHIVE_LIMIT (8u * 1024u * 1024u)
#define QR_HELPER_SIZE 3248056u
#define QR_LICENSE_SIZE 11560u

typedef struct {
    const WCHAR *name;
    uint32_t size;
    uint8_t sha[32];
    int resource;
    bool optional;
} Part;


static const Part kParts[] = {
    {L"PP-OCRv6_tiny_det-f16.gguf", 878592,
     {0x3b,0x16,0x5a,0x16,0xa3,0x3b,0x05,0x52,0x1a,0x6b,0x37,0x48,0x37,0xda,0xd9,0xbe,
      0xff,0x5e,0x8e,0x89,0x16,0x4e,0x70,0x0b,0x9a,0xbe,0x18,0x7f,0x99,0x23,0xde,0x50}},
    {L"PP-OCRv6_tiny_rec-f16.gguf", 2390272,
     {0x55,0x20,0x12,0x2d,0x9b,0xa9,0x17,0xa2,0x28,0x95,0xc4,0x8a,0x2b,0xee,0xec,0x96,
      0xff,0x4f,0x7b,0x7c,0xcb,0x61,0xa4,0x0a,0xd4,0xc7,0x11,0xbe,0x01,0x8b,0xeb,0x59}},
    {L"sniplite_ocr.dll", 0, {0}, IDR_OCR_ENGINE},
    {L"LICENSE-OCREngine.txt", 0, {0}, IDR_OCR_ENGINE_LICENSE},
    {L"crispembed.dll", 7956992,
     {0x35,0x74,0x39,0xa1,0xf4,0x3b,0x1c,0x97,0x16,0x62,0x48,0xc7,0x59,0x09,0x11,0x3c,
      0xed,0x59,0x79,0x0d,0x2a,0xae,0xe0,0xbc,0x0a,0xd8,0x0f,0x83,0x1b,0x13,0xdc,0x44}},
    {L"ggml.dll", 2817022,
     {0x75,0x70,0xf8,0x71,0x90,0x26,0x7a,0xc1,0x99,0x45,0x51,0xb3,0xa7,0x43,0x84,0x83,
      0x2c,0x54,0xf5,0x92,0xeb,0xf0,0x5d,0x63,0x28,0xfa,0x30,0x87,0xd1,0x2b,0x2c,0x3c}},
    {L"ggml-base.dll", 1416801,
     {0xf7,0xe0,0xeb,0x2d,0x64,0x52,0xa4,0xab,0x7f,0x22,0x37,0x96,0xfe,0x1d,0xe9,0x8b,
      0x1b,0x05,0x7e,0x20,0xe6,0xfc,0x38,0x24,0x56,0x82,0x2b,0xa9,0x4c,0xfc,0x94,0xd7}},
    {L"ggml-cpu.dll", 1468953,
     {0x21,0xaf,0x84,0x8b,0x66,0xef,0x91,0x35,0xb3,0x17,0x10,0x6d,0x28,0x56,0xc2,0x8c,
      0x9b,0x65,0x49,0x0e,0x16,0x13,0x2f,0x63,0x4a,0x99,0x87,0x61,0x49,0x44,0x12,0xc6}},
    {L"vcruntime140.dll", 123472,
     {0x18,0x41,0x46,0x85,0x27,0x27,0xa9,0xdb,0x4e,0xea,0x06,0x17,0x87,0x16,0xbe,0xc3,
      0xcd,0xbb,0x10,0x15,0xc9,0x11,0xf6,0xb0,0xf9,0x15,0xb1,0x84,0xad,0x77,0x75,0xb2}},
    {L"vcruntime140_1.dll", 47264,
     {0xe6,0xbf,0xb3,0x66,0x2a,0xb4,0xb1,0x96,0x9a,0x73,0x44,0x1d,0xbe,0x35,0xc9,0x6d,
      0x51,0x44,0x1b,0x6b,0xff,0x8c,0xf1,0xfe,0x74,0x30,0xbd,0x5b,0x24,0x6c,0xa6,0x05}},
    {L"msvcp140_1.dll", 35488,
     {0x2d,0xd6,0x70,0xf8,0x74,0x56,0x2f,0xbd,0xca,0x5b,0x02,0x2d,0xf1,0x94,0x3d,0x70,
      0xa5,0x7b,0xa9,0x1f,0xde,0x55,0x92,0x80,0xe3,0xa1,0xda,0xeb,0xe4,0xdb,0x23,0x80}},
    {L"msvcp140.dll", 553552,
     {0xde,0xf4,0x6a,0xa6,0xa8,0xf7,0x2f,0x27,0xba,0xfa,0xc0,0xc4,0x33,0x34,0x41,0x94,
      0x86,0xa4,0xd1,0xdc,0xdb,0x6c,0x47,0x9a,0x8e,0xf7,0x03,0x4b,0x3e,0x1f,0xa4,0xcb}},
    {L"cpu-avx2\\sniplite_ocr.dll", 819712,
     {0x0f,0x23,0xdf,0x87,0x4f,0x24,0x62,0xcf,0xe6,0x74,0x8f,0xbc,0xc9,0xca,0x04,0x74,
      0x7b,0xaf,0x88,0xf4,0xdb,0x27,0x28,0xb1,0xb5,0x57,0x1a,0xe5,0x69,0x8d,0x58,0x86}, 0, true},
    {L"cpu-avx2\\ggml.dll", 2817022,
     {0x75,0x70,0xf8,0x71,0x90,0x26,0x7a,0xc1,0x99,0x45,0x51,0xb3,0xa7,0x43,0x84,0x83,
      0x2c,0x54,0xf5,0x92,0xeb,0xf0,0x5d,0x63,0x28,0xfa,0x30,0x87,0xd1,0x2b,0x2c,0x3c}, 0, true},
    {L"cpu-avx2\\ggml-base.dll", 1416801,
     {0xf7,0xe0,0xeb,0x2d,0x64,0x52,0xa4,0xab,0x7f,0x22,0x37,0x96,0xfe,0x1d,0xe9,0x8b,
      0x1b,0x05,0x7e,0x20,0xe6,0xfc,0x38,0x24,0x56,0x82,0x2b,0xa9,0x4c,0xfc,0x94,0xd7}, 0, true},
    {L"cpu-avx2\\ggml-cpu.dll", 1691031,
     {0x33,0x16,0x6c,0xb4,0x1c,0xaf,0x90,0x4d,0x5a,0x5c,0xc3,0x98,0xb0,0xe5,0xd8,0x68,
      0x0a,0xf2,0x74,0xf6,0x77,0x22,0x21,0x31,0x2c,0x23,0x65,0x27,0x69,0xc0,0x58,0xf8}, 0, true},
};

static const WCHAR *const kNotes[] = {L"NOTICE.txt", L"Apache-2.0.txt", L"LICENSE",
                                      L"NOTICE.marker", L"NOTICE.sheenbidi", L"NOTICE.teleocr"};

static const WCHAR *const kLegacy[] = {
    L"PP-OCRv6_tiny_rec-q8-head.gguf",
    L"PP-OCRv6_small_rec-q8-head.gguf",
    L"PP-OCRv6_det_tiny.onnx", L"PP-OCRv6_rec_tiny.onnx", L"onnxruntime.dll",
    L"LICENSE-PaddleOCR.txt", L"LICENSE-onnxruntime.txt", L"ThirdPartyNotices-onnxruntime.txt",
};

/* ---- SHA-256 ---- */

static const uint32_t kSha[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t ror(uint32_t v, int n) { return v >> n | v << (32 - n); }

static void sha_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], s[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++)
        w[i] = w[i - 16] + (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3) + w[i - 7] +
               (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10);
    memcpy(s, h, sizeof s);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = s[7] + (ror(s[4], 6) ^ ror(s[4], 11) ^ ror(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + kSha[i] + w[i];
        uint32_t t2 = (ror(s[0], 2) ^ ror(s[0], 13) ^ ror(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7 * sizeof s[0]);
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++)
        h[i] += s[i];
}

static void sha256(const uint8_t *p, size_t n, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t tail[128] = {0};
    size_t whole = n & ~(size_t)63, rest = n - whole, pad = rest < 56 ? 64 : 128;
    for (size_t i = 0; i < whole; i += 64)
        sha_block(h, p + i);
    memcpy(tail, p + whole, rest);
    tail[rest] = 0x80;
    for (int i = 0; i < 8; i++)
        tail[pad - 1 - i] = (uint8_t)((uint64_t)n * 8 >> (i * 8));
    for (size_t i = 0; i < pad; i += 64)
        sha_block(h, tail + i);
    for (int i = 0; i < 32; i++)
        out[i] = (uint8_t)(h[i / 4] >> (24 - i % 4 * 8));
}

/* ---- inflate (RFC 1951): the whole input to the whole output ---- */

typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint64_t bits;
    int nbits;
    uint8_t *out;
    size_t outlen, outpos;
} Inf;

#define FAST 10 /* codes up to this long are read with one look into a table */

typedef struct {
    uint16_t count[16], symbol[288];
    uint16_t fast[1 << FAST]; /* by the next FAST bits: symbol << 4 | the code's length; 0 for a longer code */
} Huff;

static uint32_t take(Inf *s, int n)
{
    while (s->nbits < n && s->inpos < s->inlen) {
        s->bits |= (uint64_t)s->in[s->inpos++] << s->nbits;
        s->nbits += 8;
    }
    uint32_t v = (uint32_t)s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n; /* below zero past the end: caught by the callers' bounds */
    return v;
}

static void huff_build(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int l = 1; l < 15; l++)
        offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++)
        if (len[i])
            h->symbol[offs[len[i]]++] = (uint16_t)i;
    /* the table: every short code, written as its bits arrive (last bit first), with all that can follow it */
    uint16_t next[16];
    unsigned code = 0;
    for (int l = 1; l <= 15; l++) {
        code = (code + h->count[l - 1]) << 1;
        next[l] = (uint16_t)code;
    }
    memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) {
        int l = len[i];
        if (!l)
            continue;
        unsigned c = next[l]++, r = 0;
        if (l > FAST)
            continue;
        for (int b = 0; b < l; b++)
            r |= (c >> b & 1) << (l - 1 - b);
        for (unsigned j = r; j < 1u << FAST; j += 1u << l)
            h->fast[j] = (uint16_t)(i << 4 | l);
    }
}

static int huff_read(Inf *s, const Huff *h)
{
    while (s->nbits < 15 && s->inpos < s->inlen) {
        s->bits |= (uint64_t)s->in[s->inpos++] << s->nbits;
        s->nbits += 8;
    }
    unsigned e = h->fast[s->bits & ((1u << FAST) - 1)];
    if (e) {
        s->bits >>= e & 15;
        s->nbits -= (int)(e & 15);
        return (int)(e >> 4);
    }
    int code = 0, first = 0, index = 0; /* a longer code: bit by bit */
    for (int len = 1; len <= 15; len++) {
        code |= (int)take(s, 1);
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static bool inflate_codes(Inf *s, const Huff *lit, const Huff *dist)
{
    static const uint16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = huff_read(s, lit);
        if (sym < 0 || s->nbits < 0)
            return false;
        if (sym < 256) {
            if (s->outpos >= s->outlen)
                return false;
            s->out[s->outpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            if ((sym -= 257) >= 29)
                return false;
            size_t len = lbase[sym] + take(s, lext[sym]);
            int d = huff_read(s, dist);
            if (d < 0 || d >= 30)
                return false;
            size_t back = dbase[d] + take(s, dext[d]);
            if (back > s->outpos || len > s->outlen - s->outpos)
                return false;
            uint8_t *to = s->out + s->outpos;
            for (size_t i = 0; i < len; i++) /* may overlap itself: byte by byte */
                to[i] = to[i - back];
            s->outpos += len;
        }
    }
}

static bool inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Inf s = {in, inlen, 0, 0, 0, out, outlen, 0};
    Huff *lit = mem_alloc(2 * sizeof(Huff)), *dist = lit + 1;
    bool ok = lit != NULL;
    for (uint32_t last = 0; ok && !last;) {
        last = take(&s, 1);
        uint32_t type = take(&s, 2);
        uint8_t len[320];
        if (type == 0) { /* stored: from the next whole byte, giving back the bytes read ahead */
            if (s.nbits > 0)
                s.inpos -= (size_t)(s.nbits / 8);
            s.bits = 0, s.nbits = 0;
            if (s.inpos + 4 > s.inlen)
                ok = false;
            else {
                size_t n = s.in[s.inpos] | (size_t)s.in[s.inpos + 1] << 8;
                s.inpos += 4;
                ok = n <= s.inlen - s.inpos && n <= s.outlen - s.outpos;
                if (ok) {
                    memcpy(s.out + s.outpos, s.in + s.inpos, n);
                    s.inpos += n, s.outpos += n;
                }
            }
        } else if (type == 1) { /* the fixed codes */
            int i = 0;
            for (; i < 144; i++) len[i] = 8;
            for (; i < 256; i++) len[i] = 9;
            for (; i < 280; i++) len[i] = 7;
            for (; i < 288; i++) len[i] = 8;
            huff_build(lit, len, 288);
            memset(len, 5, 30);
            huff_build(dist, len, 30);
            ok = inflate_codes(&s, lit, dist);
        } else if (type == 2) { /* codes sent with the block */
            int nlen = (int)take(&s, 5) + 257, ndist = (int)take(&s, 5) + 1, ncode = (int)take(&s, 4) + 4, i = 0;
            memset(len, 0, sizeof len);
            for (; i < ncode; i++)
                len[order[i]] = (uint8_t)take(&s, 3);
            huff_build(lit, len, 19);
            for (i = 0; ok && i < nlen + ndist;) {
                int sym = huff_read(&s, lit), rep = 1, v = sym;
                if (sym < 0 || s.nbits < 0)
                    ok = false;
                else if (sym == 16) {
                    ok = i > 0;
                    v = ok ? len[i - 1] : 0;
                    rep = 3 + (int)take(&s, 2);
                } else if (sym == 17) {
                    v = 0, rep = 3 + (int)take(&s, 3);
                } else if (sym == 18) {
                    v = 0, rep = 11 + (int)take(&s, 7);
                }
                if (i + rep > nlen + ndist)
                    ok = false;
                for (; ok && rep > 0; rep--)
                    len[i++] = (uint8_t)v;
            }
            if (ok) {
                uint8_t dl[32];
                memcpy(dl, len + nlen, (size_t)ndist);
                huff_build(lit, len, nlen);
                huff_build(dist, dl, ndist);
                ok = inflate_codes(&s, lit, dist);
            }
        } else {
            ok = false;
        }
    }
    mem_free(lit);
    return ok && s.outpos == outlen;
}

/* ---- the zip ---- */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static bool part_path(const WCHAR *name, const WCHAR *suffix, WCHAR *path, int cap)
{
    WCHAR dir[MAX_PATH * 2];
    ocr_dir(dir, COUNTOF(dir));
    if (lstrlenW(dir) + lstrlenW(name) + lstrlenW(suffix) + 2 > cap)
        return false;
    wsprintfW(path, L"%s\\%s%s", dir, name, suffix);
    return true;
}

/* A ZIP entry into its destination folder: written under another name, then given its own. */
static bool file_write(const WCHAR *dir, const WCHAR *name, const uint8_t *data, uint32_t n)
{
    WCHAR path[MAX_PATH * 2], tmp[MAX_PATH * 2], child[MAX_PATH * 2];
    if (lstrlenW(dir) + lstrlenW(name) + 8 >= COUNTOF(path))
        return false;
    if (lstrlenW(name) > 9 && name[8] == L'\\') {
        wsprintfW(child, L"%s\\cpu-avx2", dir);
        if (!CreateDirectoryW(child, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    wsprintfW(path, L"%s\\%s", dir, name);
    wsprintfW(tmp, L"%s.part", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote = 0;
    bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, data, n, &wrote, NULL) && wrote == n;
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

/* Extract only the known OCR files from the archive; all model and DLL bytes are pinned. */
static int zip_unpack(const uint8_t *zip, size_t n)
{
    if (n < 22 || rd32(zip + n - 22) != 0x06054b50) return OCR_GET_CHECK;
    const uint8_t *end = zip + n - 22;
    uint32_t count = rd16(end + 10), dir_bytes = rd32(end + 12), dir_at = rd32(end + 16);
    if ((uint64_t)dir_at + dir_bytes > n - 22) return OCR_GET_CHECK;
    WCHAR dir[MAX_PATH * 2];
    ocr_dir(dir, COUNTOF(dir));
    if (!dir[0] || (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) return OCR_GET_WRITE;
    bool shared_runtime = formula_installed();
    const uint8_t *e = zip + dir_at, *dir_end = e + dir_bytes;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (dir_end - e < 46 || rd32(e) != 0x02014b50) return OCR_GET_CHECK;
        uint32_t method = rd16(e + 10), csize = rd32(e + 20), usize = rd32(e + 24);
        uint32_t nlen = rd16(e + 28), at = rd32(e + 42);
        const char *name = (const char *)e + 46;
        if ((size_t)(dir_end - e) < 46 + (size_t)nlen + rd16(e + 30) + rd16(e + 32)) return OCR_GET_CHECK;
        e += 46 + nlen + rd16(e + 30) + rd16(e + 32);
        WCHAR file[64];
        bool plain = nlen > 4 && nlen < 4 + COUNTOF(file) && !memcmp(name, "ocr/", 4) && name[4] != '.';
        bool nested = false;
        for (uint32_t c = 4; plain && c < nlen; c++) {
            char ch = name[c];
            if (ch == '/') {
                plain = !nested && c > 4;
                nested = true;
                file[c - 4] = L'\\';
            } else {
                plain = ch > 32 && ch < 127 && ch != '\\' && ch != ':';
                file[c - 4] = (WCHAR)ch;
            }
        }
        if (!plain) continue;
        file[nlen - 4] = 0;
        const Part *part = NULL;
        int part_index = -1;
        for (int k = 0; k < COUNTOF(kParts); k++)
            if (!lstrcmpiW(file, kParts[k].name)) { part = &kParts[k]; part_index = k; break; }
        bool note = false;
        for (int k = 0; k < COUNTOF(kNotes); k++) note |= !lstrcmpiW(file, kNotes[k]);
        if (!part && !note) continue;
        const void *embedded = NULL;
        DWORD expected = part ? part->size : 0;
        if (part && part->resource) {
            HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(part->resource), RT_RCDATA);
            HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
            embedded = g ? LockResource(g) : NULL;
            expected = r ? SizeofResource(NULL, r) : 0;
            if (!embedded || !expected) return OCR_GET_CHECK;
        }
        if (part) {
            uint32_t bit = 1u << part_index;
            if ((seen & bit) || usize != expected) return OCR_GET_CHECK;
            seen |= bit;
        }
        if ((uint64_t)at + 30 > dir_at || rd32(zip + at) != 0x04034b50) return OCR_GET_CHECK;
        const uint8_t *data = zip + at + 30 + rd16(zip + at + 26) + rd16(zip + at + 28);
        if (data > zip + dir_at || (size_t)(zip + dir_at - data) < csize) return OCR_GET_CHECK;
        uint8_t *mem = method == 8 ? mem_alloc(usize ? usize : 1) : NULL, sha[32];
        const uint8_t *out = mem ? mem : data;
        int result = method == 8 ? (mem && inflate(data, csize, mem, usize) ? OCR_GET_OK : OCR_GET_CHECK)
                                 : method == 0 && csize == usize ? OCR_GET_OK : OCR_GET_CHECK;
        if (result == OCR_GET_OK && part) {
            if (embedded) {
                if (memcmp(out, embedded, usize)) result = OCR_GET_CHECK;
            } else {
                sha256(out, usize, sha);
                if (memcmp(sha, part->sha, 32)) result = OCR_GET_CHECK;
            }
        }
        if (result == OCR_GET_OK && !(part && part_index >= 4 && !part->optional && shared_runtime) && !file_write(dir, file, out, usize))
            result = OCR_GET_WRITE;
        mem_free(mem);
        if (result != OCR_GET_OK) return result;
    }
    uint32_t required = 0;
    for (int k = 0; k < COUNTOF(kParts); k++)
        if (!kParts[k].optional) required |= 1u << k;
    return (seen & required) == required && ocr_installed() ? OCR_GET_OK : OCR_GET_CHECK;
}

/* ---- downloaded ---- */

typedef struct {
    uint8_t *buf;
    uint32_t have, total;
    volatile LONG *cancel;
    void (*progress)(int, void *);
    void *ctx;
    int shown;
} Fetch;

static bool fetch_sink(const void *data, DWORD n, void *ctx)
{
    Fetch *f = ctx;
    if (*f->cancel || n > f->total - f->have)
        return false; /* given up, or not the file that was published */
    memcpy(f->buf + f->have, data, n);
    f->have += n;
    int pct = (int)((uint64_t)f->have * 100 / f->total);
    if (pct != f->shown && f->progress)
        f->progress(f->shown = pct, f->ctx);
    return true;
}

/* Download the pinned module ZIP, then validate its contents before installing them. */
static int get(const WCHAR *path, uint32_t bytes, volatile LONG *cancel,
               void (*progress)(int percent, void *ctx), void *ctx)
{
    Fetch f = {mem_alloc(bytes), 0, bytes, cancel, progress, ctx, -1};
    DWORD status;
    if (!f.buf) return OCR_GET_WRITE;
    bool whole = http_fetch(UPDATE_HOST, path, NULL, &status, fetch_sink, &f) && f.have == bytes;
    int result = *cancel ? OCR_GET_CANCEL : !whole ? OCR_GET_NET : zip_unpack(f.buf, f.have);
    mem_free(f.buf);
    return result;
}

int ocr_get(volatile LONG *cancel, void (*progress)(int percent, void *ctx), void *ctx)
{
    LONG never_cancel = 0;
    if (!cancel) cancel = &never_cancel;
    return get(MODULE_PATH, MODULE_BYTES, cancel, progress, ctx);
}

/* ---- carried by the program itself ----
 *
 * sniplite_<version>_portable_with_OCR_x64.exe is sniplite.exe with the zip put after it
 * (package-all.bat: copy /b), so the module travels in the one file. The zip's last record says how
 * long its directory is and where that begins, counted from the zip's own start: so where the
 * zip begins in the program's file is known from the file's end. The first time text is to be
 * read, the folder is written beside the program. A sniplite.exe without the zip is not slowed
 * by any of this: the last 22 bytes of the file are looked at once. */

typedef struct {
    HANDLE f;
    uint64_t at; /* where the zip begins in the file */
    uint32_t bytes;
} Pack;

static bool read_at(HANDLE f, uint64_t at, void *buf, uint32_t n)
{
    LARGE_INTEGER li;
    DWORD got;
    li.QuadPart = (LONGLONG)at;
    return SetFilePointerEx(f, li, NULL, FILE_BEGIN) && ReadFile(f, buf, n, &got, NULL) && got == n;
}

static bool pack_open(Pack *k)
{
    WCHAR exe[MAX_PATH * 2];
    LARGE_INTEGER size;
    uint8_t end[22];
    k->f = GetModuleFileNameW(NULL, exe, COUNTOF(exe))
               ? CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL)
               : INVALID_HANDLE_VALUE;
    if (k->f == INVALID_HANDLE_VALUE)
        return false;
    if (GetFileSizeEx(k->f, &size) && size.QuadPart > 22 && read_at(k->f, (uint64_t)size.QuadPart - 22, end, 22) &&
        rd32(end) == 0x06054b50) {
        uint64_t zip = (uint64_t)rd32(end + 16) + rd32(end + 12) + 22; /* all of it: up to its directory, that, the last record */
        k->at = (uint64_t)size.QuadPart - zip;
        k->bytes = (uint32_t)zip;
        if (zip < (uint64_t)size.QuadPart && zip < 0x40000000)
            return true;
    }
    CloseHandle(k->f);
    return false;
}

bool ocr_packed(void)
{
    static int known; /* 1 no, 2 yes: the program's file does not change while it runs */
    if (!known) {
        Pack k;
        known = pack_open(&k) ? 2 : 1;
        if (known == 2)
            CloseHandle(k.f);
    }
    return known == 2;
}

bool ocr_unpack(void)
{
    Pack k;
    if (!pack_open(&k))
        return false;
    uint8_t *zip = mem_alloc(k.bytes);
    bool ok = zip && read_at(k.f, k.at, zip, k.bytes) && zip_unpack(zip, k.bytes) == OCR_GET_OK;
    mem_free(zip);
    CloseHandle(k.f);
    return ok;
}


/* ---- removed ---- */

bool ocr_remove(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2];
    ocr_dir(dir, COUNTOF(dir));
    for (int i = COUNTOF(kParts) - 1; i >= 0; i--) { /* the DLLs first: a helper still there holds them, and then nothing goes */
        if (part_path(kParts[i].name, L"", path, COUNTOF(path)) && !DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PATH_NOT_FOUND) /* neither the file nor the folder there: nothing to remove */
            return false;
        if (part_path(kParts[i].name, L".part", path, COUNTOF(path)))
            DeleteFileW(path);
    }
    for (int i = 0; i < COUNTOF(kLegacy); i++)
        if (part_path(kLegacy[i], L"", path, COUNTOF(path)) && !DeleteFileW(path) &&
            GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
            return false;
    for (int i = 0; i < COUNTOF(kNotes); i++)
        if (part_path(kNotes[i], L"", path, COUNTOF(path)))
            DeleteFileW(path);
    if (lstrlenW(dir) + 10 < COUNTOF(path)) {
        wsprintfW(path, L"%s\\cpu-avx2", dir);
        RemoveDirectoryW(path);
    }
    RemoveDirectoryW(dir); /* stays when something else is in it */
    if (!formula_installed()) return formula_remove();
    return true;
}

int ocr_size_mb(void)
{
    uint64_t sum = 0;
    WCHAR path[MAX_PATH * 2];
    for (int i = 0; i < COUNTOF(kParts); i++) {
        if (!part_path(kParts[i].name, L"", path, COUNTOF(path))) continue;
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &data))
            sum += ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    }
    return (int)((sum + 512 * 1024) >> 20);
}

/* ---- the optional QR helper: extracted from this version's portable QR ZIP ---- */

static bool qr_path(const WCHAR *name, const WCHAR *suffix, WCHAR *path, int cap)
{
    WCHAR exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, exe, COUNTOF(exe));
    if (!n || n >= COUNTOF(exe))
        return false;
    while (n && exe[n - 1] != L'\\' && exe[n - 1] != L'/')
        n--;
    if (!n || n + lstrlenW(name) + lstrlenW(suffix) + 1 > (DWORD)cap)
        return false;
    exe[n] = 0;
    wsprintfW(path, L"%s%s%s", exe, name, suffix);
    return true;
}

static bool qr_file_size(const WCHAR *name, uint64_t *size)
{
    WCHAR path[MAX_PATH * 2];
    if (!qr_path(name, L"", path, COUNTOF(path)))
        return false;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER n;
    bool ok = GetFileSizeEx(h, &n) && n.QuadPart >= 0;
    CloseHandle(h);
    if (ok)
        *size = (uint64_t)n.QuadPart;
    return ok;
}

bool qr_installed(void)
{
    uint64_t exe, license;
    return qr_file_size(L"sniplite_qr.exe", &exe) && exe == QR_HELPER_SIZE &&
           qr_file_size(L"LICENSE-ZXing.txt", &license) && license == QR_LICENSE_SIZE;
}

int qr_size_mb(void)
{
    uint64_t exe = 0, license = 0;
    qr_file_size(L"sniplite_qr.exe", &exe);
    qr_file_size(L"LICENSE-ZXing.txt", &license);
    return (int)((exe + license + 512 * 1024) >> 20);
}

static bool qr_delete_file(const WCHAR *name, const WCHAR *suffix)
{
    WCHAR path[MAX_PATH * 2];
    if (!qr_path(name, suffix, path, COUNTOF(path)))
        return false;
    if (DeleteFileW(path))
        return true;
    DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

bool qr_remove(void)
{
    bool ok = qr_delete_file(L"sniplite_qr.exe", L"");
    ok = qr_delete_file(L"LICENSE-ZXing.txt", L"") && ok;
    qr_delete_file(L"sniplite_qr.exe", L".part");
    qr_delete_file(L"LICENSE-ZXing.txt", L".part");
    return ok;
}

static bool qr_file_write(const WCHAR *name, const uint8_t *data, uint32_t n)
{
    WCHAR path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!qr_path(name, L"", path, COUNTOF(path)) || !qr_path(name, L".part", tmp, COUNTOF(tmp)))
        return false;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote = 0;
    bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, data, n, &wrote, NULL) && wrote == n;
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

static bool qr_entry(const uint8_t *zip, size_t dir_at, const uint8_t *e, uint32_t nlen, uint32_t at,
                     uint32_t csize, uint32_t usize, uint32_t method, uint32_t flags,
                     const char *name, uint32_t expected, const uint8_t digest[32],
                     const uint8_t **data, uint8_t **owned)
{
    size_t name_len = lstrlenA(name);
    if (nlen != name_len || memcmp(e + 46, name, name_len) || usize != expected || (flags & 0x41) ||
        (method != 0 && method != 8) || (uint64_t)at + 30 > dir_at || rd32(zip + at) != 0x04034b50)
        return false;
    uint32_t local_nlen = rd16(zip + at + 26), local_extra = rd16(zip + at + 28);
    uint64_t pos = (uint64_t)at + 30 + local_nlen + local_extra;
    if (local_nlen != nlen || pos > dir_at || memcmp(zip + at + 30, name, name_len) || csize > dir_at - pos)
        return false;
    const uint8_t *src = zip + (size_t)pos;
    uint8_t *out = NULL;
    if (method == 8) {
        out = mem_alloc(usize);
        if (!out || !inflate(src, csize, out, usize)) {
            mem_free(out);
            return false;
        }
        src = out;
    } else if (csize != usize) {
        return false;
    }
    uint8_t sha[32];
    sha256(src, usize, sha);
    if (memcmp(sha, digest, sizeof sha)) {
        mem_free(out);
        return false;
    }
    *data = src;
    *owned = out;
    return true;
}

static int qr_zip_unpack(const uint8_t *zip, size_t n)
{
    static const uint8_t helper_sha[32] = {
        0x52,0x54,0x2e,0xf7,0x67,0x5e,0x46,0x23,0x55,0x29,0x3f,0x4b,0x76,0xdf,0x2e,0x00,
        0x23,0x25,0x3c,0xc8,0xc2,0x17,0xd6,0x83,0x0e,0x71,0x57,0xf6,0xad,0x5a,0xe4,0x46};
    static const uint8_t license_sha[32] = {
        0x2e,0x54,0xcd,0x84,0xa6,0x45,0xbe,0xa2,0x59,0x43,0xc7,0x5d,0xd8,0xae,0x67,0xcb,
        0x29,0x1e,0x66,0xa4,0x7a,0x11,0x57,0x83,0x33,0xc9,0xb4,0xb3,0xb6,0xb8,0x6c,0x85};
    if (n < 22 || rd32(zip + n - 22) != 0x06054b50 || rd16(zip + n - 22 + 4) || rd16(zip + n - 22 + 6) ||
        rd16(zip + n - 22 + 8) != rd16(zip + n - 22 + 10))
        return QR_GET_CHECK;
    const uint8_t *end = zip + n - 22;
    uint32_t count = rd16(end + 10), dir_bytes = rd32(end + 12), dir_at = rd32(end + 16);
    if ((uint64_t)dir_at + dir_bytes != n - 22)
        return QR_GET_CHECK;
    const uint8_t *e = zip + dir_at, *dir_end = e + dir_bytes;
    const uint8_t *helper = NULL, *license = NULL;
    uint8_t *helper_mem = NULL, *license_mem = NULL;
    bool got_helper = false, got_license = false;
    int result = QR_GET_CHECK;
    for (uint32_t i = 0; i < count; i++) {
        if (dir_end - e < 46 || rd32(e) != 0x02014b50)
            goto done;
        uint32_t method = rd16(e + 10), flags = rd16(e + 8), csize = rd32(e + 20), usize = rd32(e + 24);
        uint32_t nlen = rd16(e + 28), at = rd32(e + 42);
        if ((size_t)(dir_end - e) < 46 + (size_t)nlen + rd16(e + 30) + rd16(e + 32))
            goto done;
        const uint8_t *entry = e;
        e += 46 + nlen + rd16(e + 30) + rd16(e + 32);
        if (nlen == sizeof("sniplite_qr.exe") - 1 && !memcmp(entry + 46, "sniplite_qr.exe", nlen)) {
            if (got_helper || !qr_entry(zip, dir_at, entry, nlen, at, csize, usize, method, flags,
                                        "sniplite_qr.exe", QR_HELPER_SIZE, helper_sha, &helper, &helper_mem))
                goto done;
            got_helper = true;
        } else if (nlen == sizeof("LICENSE-ZXing.txt") - 1 && !memcmp(entry + 46, "LICENSE-ZXing.txt", nlen)) {
            if (got_license || !qr_entry(zip, dir_at, entry, nlen, at, csize, usize, method, flags,
                                         "LICENSE-ZXing.txt", QR_LICENSE_SIZE, license_sha, &license, &license_mem))
                goto done;
            got_license = true;
        }
    }
    if (!got_helper || !got_license)
        goto done;
    if (!qr_file_write(L"LICENSE-ZXing.txt", license, QR_LICENSE_SIZE) ||
        !qr_file_write(L"sniplite_qr.exe", helper, QR_HELPER_SIZE)) {
        result = QR_GET_WRITE;
        goto done;
    }
    result = QR_GET_OK;
done:
    mem_free(helper_mem);
    mem_free(license_mem);
    return result;
}

typedef struct {
    uint8_t *buf;
    size_t have;
    volatile LONG *cancel;
} QrFetch;

static bool qr_fetch_sink(const void *data, DWORD n, void *ctx)
{
    QrFetch *f = ctx;
    if (*f->cancel || n > QR_ARCHIVE_LIMIT - f->have)
        return false;
    memcpy(f->buf + f->have, data, n);
    f->have += n;
    return true;
}

int qr_get(volatile LONG *cancel, void (*progress)(int, void *), void *ctx)
{
    QrFetch f = {mem_alloc(QR_ARCHIVE_LIMIT), 0, cancel};
    if (!f.buf)
        return QR_GET_WRITE;
    if (progress)
        progress(-1, ctx);
    DWORD status = 0;
    bool whole = http_fetch(UPDATE_HOST, QR_MODULE_PATH, NULL, &status, qr_fetch_sink, &f);
    int result = *cancel ? QR_GET_CANCEL : !whole ? QR_GET_NET : qr_zip_unpack(f.buf, f.have);
    mem_free(f.buf);
    if (result == QR_GET_OK && progress)
        progress(100, ctx);
    return result;
}
