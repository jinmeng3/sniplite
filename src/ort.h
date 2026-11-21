/* The few entries of ONNX Runtime's C API that text recognition uses.
 *
 * The API is one table of function pointers. Its order never changes between
 * releases (new functions are appended), so a slot number holds for every runtime
 * at least as new as the API version asked for. The numbers below were read from
 * the official onnxruntime_c_api.h by tests/ocr/ort_slots.py; that header is not
 * needed to build. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#define ORT_API_WANTED 12 /* ONNX Runtime 1.12 or newer */

typedef struct {
    const void *(*GetApi)(uint32_t version); /* NULL when the runtime is older than that */
    const char *(*GetVersionString)(void);
} OrtApiBase;

/* all of them opaque */
typedef struct OrtStatus OrtStatus; /* NULL means success */
typedef struct OrtEnv OrtEnv;
typedef struct OrtSession OrtSession;
typedef struct OrtSessionOptions OrtSessionOptions;
typedef struct OrtMemoryInfo OrtMemoryInfo;
typedef struct OrtAllocator OrtAllocator;
typedef struct OrtValue OrtValue;
typedef struct OrtShape OrtShape; /* OrtTensorTypeAndShapeInfo */
typedef struct OrtModelMetadata OrtModelMetadata;

enum {
    ORT_LOG_WARNING = 2,
    ORT_OPTIMIZE_ALL = 99,
    ORT_ARENA_ALLOCATOR = 1,
    ORT_MEM_DEFAULT = 0,
    ORT_FLOAT = 1,
    ORT_INT64 = 7,
    ORT_BOOL = 9,
};

/* X(slot, name, return type, parameters) */
#define ORT_FUNCTIONS(X)                                                                                               \
    X(2, GetErrorMessage, const char *, (const OrtStatus *))                                                           \
    X(3, CreateEnv, OrtStatus *, (int log_level, const char *log_id, OrtEnv **))                                       \
    X(7, CreateSession, OrtStatus *, (const OrtEnv *, const wchar_t *path, const OrtSessionOptions *, OrtSession **))  \
    X(9, Run, OrtStatus *,                                                                                             \
      (OrtSession *, const void *run_options, const char *const *in_names, const OrtValue *const *in, size_t nin,      \
       const char *const *out_names, size_t nout, OrtValue **out))                                                     \
    X(10, CreateSessionOptions, OrtStatus *, (OrtSessionOptions **))                                                       X(11, SetOptimizedModelFilePath, OrtStatus *, (OrtSessionOptions *, const wchar_t *path))                             X(17, DisableMemPattern, OrtStatus *, (OrtSessionOptions *))                                                       \
    X(23, SetSessionGraphOptimizationLevel, OrtStatus *, (OrtSessionOptions *, int level))                            \
    X(24, SetIntraOpNumThreads, OrtStatus *, (OrtSessionOptions *, int n))                                             \
    X(36, SessionGetInputName, OrtStatus *, (const OrtSession *, size_t index, OrtAllocator *, char **))               \
    X(37, SessionGetOutputName, OrtStatus *, (const OrtSession *, size_t index, OrtAllocator *, char **))              \
    X(49, CreateTensorWithDataAsOrtValue, OrtStatus *,                                                                 \
      (const OrtMemoryInfo *, void *data, size_t bytes, const int64_t *shape, size_t ndim, int type, OrtValue **))     \
    X(51, GetTensorMutableData, OrtStatus *, (OrtValue *, void **))                                                    \
    X(61, GetDimensionsCount, OrtStatus *, (const OrtShape *, size_t *))                                               \
    X(62, GetDimensions, OrtStatus *, (const OrtShape *, int64_t *dims, size_t ndim))                                  \
    X(65, GetTensorTypeAndShape, OrtStatus *, (const OrtValue *, OrtShape **))                                         \
    X(69, CreateCpuMemoryInfo, OrtStatus *, (int allocator_type, int mem_type, OrtMemoryInfo **))                      \
    X(76, AllocatorFree, OrtStatus *, (OrtAllocator *, void *))                                                        \
    X(78, GetAllocatorWithDefaultOptions, OrtStatus *, (OrtAllocator **))                                              \
    X(92, ReleaseEnv, void, (OrtEnv *))                                                                                \
    X(93, ReleaseStatus, void, (OrtStatus *))                                                                          \
    X(94, ReleaseMemoryInfo, void, (OrtMemoryInfo *))                                                                  \
    X(95, ReleaseSession, void, (OrtSession *))                                                                        \
    X(96, ReleaseValue, void, (OrtValue *))                                                                            \
    X(99, ReleaseShape, void, (OrtShape *))                                                                            \
    X(100, ReleaseSessionOptions, void, (OrtSessionOptions *))                                                         \
    X(111, SessionGetModelMetadata, OrtStatus *, (const OrtSession *, OrtModelMetadata **))                            \
    X(116, ModelMetadataLookupCustomMetadataMap, OrtStatus *,                                                          \
      (const OrtModelMetadata *, OrtAllocator *, const char *key, char **value))                                       \
    X(118, ReleaseModelMetadata, void, (OrtModelMetadata *))     X(130, AddSessionConfigEntry, OrtStatus *, (OrtSessionOptions *, const char *key, const char *value))

/* the same functions as a structure of typed pointers, filled by ort_bind() */
typedef struct {
#define X(slot, name, ret, params) ret(*name) params;
    ORT_FUNCTIONS(X)
#undef X
} Ort;

static inline void ort_bind(Ort *o, const void *api)
{
    void *const *table = (void *const *)api;
#define X(slot, name, ret, params) o->name = (ret(*) params)table[slot];
    ORT_FUNCTIONS(X)
#undef X
}
