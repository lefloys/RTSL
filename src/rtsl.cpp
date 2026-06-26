#include "rtsl.h"

#include "Compiler/Compiler.h"
#include "Link/Linker.h"

#include <exception>
#include <memory>
#include <string>
#include <vector>

struct rtsl_module_t {
    rtsl::Artifact artifact;
};

struct rtsl_context_t {
    rtsl::CompilerInstance compiler;
    rtsl_result result{RTSL_OK, "ok"};
};

struct rtsl_linker_t {
    rtsl_context_t *ctx = nullptr;
    rtsl::Linker linker;

    explicit rtsl_linker_t(rtsl_context_t *context)
        : ctx(context), linker(context->compiler.diagnostics()) {}
};

namespace {

void set_result(rtsl_context ctx, int code, const char *text) {
    if (ctx) {
        ctx->result = rtsl_result{code, text};
    }
}

rtsl_output_kind to_c_kind(rtsl::ArtifactKind kind) {
    switch (kind) {
    case rtsl::ArtifactKind::object: return RTSL_OUTPUT_OBJECT;
    case rtsl::ArtifactKind::module: return RTSL_OUTPUT_MODULE;
    case rtsl::ArtifactKind::library: return RTSL_OUTPUT_LIBRARY;
    case rtsl::ArtifactKind::program: return RTSL_OUTPUT_PROGRAM;
    }
    return RTSL_OUTPUT_OBJECT;
}

rtsl_diagnostic_severity to_c_severity(rtsl::DiagnosticSeverity severity) {
    switch (severity) {
    case rtsl::DiagnosticSeverity::ignored: return RTSL_DIAG_IGNORED;
    case rtsl::DiagnosticSeverity::note: return RTSL_DIAG_NOTE;
    case rtsl::DiagnosticSeverity::warning: return RTSL_DIAG_WARNING;
    case rtsl::DiagnosticSeverity::error: return RTSL_DIAG_ERROR;
    case rtsl::DiagnosticSeverity::fatal: return RTSL_DIAG_FATAL;
    }
    return RTSL_DIAG_ERROR;
}

} // namespace

extern "C" {

rtsl_context rtslCreateContext(void) {
    try {
        return new rtsl_context_t();
    } catch (...) {
        return nullptr;
    }
}

rtsl_result rtslCtxGetResult(rtsl_context ctx) {
    return ctx ? ctx->result : rtsl_result{RTSL_ERROR_INVALID_ARGUMENT, "null context"};
}

size_t rtslCtxGetDiagnosticCount(rtsl_context ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->compiler.diagnostics().diagnostics().size();
}

rtsl_diagnostic rtslCtxGetDiagnostic(rtsl_context ctx, size_t index) {
    if (!ctx || index >= ctx->compiler.diagnostics().diagnostics().size()) {
        return {};
    }
    const auto &diagnostic = ctx->compiler.diagnostics().diagnostics()[index];
    return rtsl_diagnostic{
        .code = diagnostic.code,
        .severity = to_c_severity(diagnostic.severity),
        .source_name = diagnostic.source_name.c_str(),
        .offset = diagnostic.location.offset,
        .line = diagnostic.location.line,
        .column = diagnostic.location.column,
        .text = diagnostic.message.c_str(),
    };
}

void rtslDestroyContext(rtsl_context ctx) {
    delete ctx;
}

rtsl_module rtslCompileSource(rtsl_context ctx, const char *source, size_t source_size, const char *source_name) {
    if (!ctx || (!source && source_size != 0)) {
        set_result(ctx, RTSL_ERROR_INVALID_ARGUMENT, "invalid compile arguments");
        return nullptr;
    }

    try {
        rtsl::CompilerInvocation invocation;
        invocation.source_name = source_name ? source_name : "<memory>";
        auto artifact = ctx->compiler.compile_source(std::string(source ? source : "", source_size), std::move(invocation));
        if (ctx->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
            set_result(ctx, RTSL_ERROR_COMPILE_FAILED, "compile failed");
            return nullptr;
        }
        set_result(ctx, RTSL_OK, "ok");
        return new rtsl_module_t{.artifact = std::move(artifact)};
    } catch (const std::bad_alloc &) {
        set_result(ctx, RTSL_ERROR_INTERNAL, "allocation failed");
        return nullptr;
    } catch (...) {
        set_result(ctx, RTSL_ERROR_INTERNAL, "internal compiler error");
        return nullptr;
    }
}

rtsl_blob rtslModuleGetBytecode(rtsl_module module) {
    if (!module) {
        return {};
    }
    return rtsl_blob{module->artifact.bytes.data(), module->artifact.bytes.size()};
}

rtsl_output_kind rtslModuleGetKind(rtsl_module module) {
    return module ? to_c_kind(module->artifact.kind) : RTSL_OUTPUT_OBJECT;
}

void rtslDestroyModule(rtsl_module module) {
    delete module;
}

rtsl_linker rtslCreateLinker(rtsl_context ctx) {
    if (!ctx) {
        return nullptr;
    }
    try {
        return new rtsl_linker_t(ctx);
    } catch (...) {
        set_result(ctx, RTSL_ERROR_INTERNAL, "failed to create linker");
        return nullptr;
    }
}

int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module) {
    if (!linker || !module) {
        return 0;
    }
    return linker->linker.add_artifact(module->artifact) ? 1 : 0;
}

int rtslLinkerAddBlob(rtsl_linker linker, const uint8_t *data, size_t size) {
    if (!linker || (!data && size != 0)) {
        return 0;
    }
    return linker->linker.add_artifact_bytes(std::span<const rtsl::u8>(data, size)) ? 1 : 0;
}

rtsl_module rtslLinkProgram(rtsl_linker linker) {
    if (!linker) {
        return nullptr;
    }
    try {
        auto artifact = linker->linker.link_program();
        if (linker->ctx->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
            set_result(linker->ctx, RTSL_ERROR_LINK_FAILED, "link failed");
            return nullptr;
        }
        set_result(linker->ctx, RTSL_OK, "ok");
        return new rtsl_module_t{.artifact = std::move(artifact)};
    } catch (...) {
        set_result(linker->ctx, RTSL_ERROR_INTERNAL, "internal linker error");
        return nullptr;
    }
}

void rtslDestroyLinker(rtsl_linker linker) {
    delete linker;
}

} // extern "C"
