#ifndef RTSL_H
#define RTSL_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#    if defined(RTSL_BUILD_SHARED)
#        define RTSL_API __declspec(dllexport)
#    elif defined(RTSL_SHARED)
#        define RTSL_API __declspec(dllimport)
#    else
#        define RTSL_API
#    endif
#else
#    define RTSL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtsl_module_t* rtsl_module;
typedef struct rtsl_context_t* rtsl_context;
typedef struct rtsl_linker_t* rtsl_linker;

typedef enum rtsl_result_code {
    RTSL_OK = 0,
    RTSL_ERROR_INVALID_ARGUMENT = 1,
    RTSL_ERROR_COMPILE_FAILED = 2,
    RTSL_ERROR_LINK_FAILED = 3,
    RTSL_ERROR_INTERNAL = 4
} rtsl_result_code;

typedef struct rtsl_result {
    int code;
    const char* text;
} rtsl_result;

typedef struct rtsl_blob {
    const uint8_t* data;
    size_t size;
} rtsl_blob;

typedef enum rtsl_output_kind {
    RTSL_OUTPUT_OBJECT = 1,
    RTSL_OUTPUT_MODULE = 2,
    RTSL_OUTPUT_LIBRARY = 3,
    RTSL_OUTPUT_PROGRAM = 4
} rtsl_output_kind;

typedef enum rtsl_diagnostic_severity {
    RTSL_DIAG_IGNORED = 0,
    RTSL_DIAG_NOTE = 1,
    RTSL_DIAG_WARNING = 2,
    RTSL_DIAG_ERROR = 3,
    RTSL_DIAG_FATAL = 4
} rtsl_diagnostic_severity;

typedef struct rtsl_diagnostic {
    int code;
    rtsl_diagnostic_severity severity;
    const char* source_name;
    size_t offset;
    uint32_t line;
    uint32_t column;
    const char* text;
} rtsl_diagnostic;

RTSL_API rtsl_context rtslCreateContext(void);
RTSL_API rtsl_result rtslCtxGetResult(rtsl_context ctx);
RTSL_API size_t rtslCtxGetDiagnosticCount(rtsl_context ctx);
RTSL_API rtsl_diagnostic rtslCtxGetDiagnostic(rtsl_context ctx, size_t index);
RTSL_API void rtslDestroyContext(rtsl_context ctx);

RTSL_API rtsl_module rtslCompileSource(rtsl_context ctx, const char* source, size_t source_size, const char* source_name);

RTSL_API rtsl_blob rtslModuleGetBytecode(rtsl_module module);
RTSL_API rtsl_output_kind rtslModuleGetKind(rtsl_module module);
RTSL_API void rtslDestroyModule(rtsl_module module);

RTSL_API rtsl_linker rtslCreateLinker(rtsl_context ctx);
RTSL_API int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module);
RTSL_API int rtslLinkerAddBlob(rtsl_linker linker, const uint8_t* data, size_t size);
RTSL_API rtsl_module rtslLinkProgram(rtsl_linker linker);
RTSL_API void rtslDestroyLinker(rtsl_linker linker);

#ifdef __cplusplus
}
#endif

#endif /* RTSL_H */
