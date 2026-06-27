#include "Compiler/Compiler.h"

#include "IR/IR.h"
#include "Lex/Lexer.h"
#include "Parse/Parser.h"
#include "Sema/Sema.h"

namespace rtsl {

Artifact CompilerInstance::compile_source(std::string_view source, CompilerInvocation invocation) {
    Artifact artifact;
    compile_source_to(artifact, source, std::move(invocation));
    return artifact;
}

void CompilerInstance::compile_source_to(Artifact& artifact, std::string_view source, CompilerInvocation invocation) {
    diagnostics_.clear();
    artifact = Artifact{.kind = ArtifactKind::object};
    const auto file_id = sources_.add_buffer(std::move(invocation.source_name), std::string(source));

    Lexer lexer(sources_, diagnostics_, file_id);
    const auto tokens = lexer.lex();

    Parser parser(sources_, diagnostics_, file_id, tokens);
    auto ast = parser.parse_translation_unit();

    Sema sema(sources_, diagnostics_);
    auto semantic_module = sema.analyze(ast);
    auto ir = lower_to_ir(semantic_module, &diagnostics_);

    if (!diagnostics_.has_error() && verify_ir(ir)) {
        artifact.bytes = write_artifact(ArtifactKind::object, ir);
        artifact.strings.clear();
        artifact.structs = ir.structs;
        artifact.uniforms = ir.uniforms;
        artifact.stage_interfaces = ir.stage_interfaces;
        artifact.functions = ir.functions;
        artifact.debug_bytes = write_debug_artifact(ir);
    }
}

} // namespace rtsl
