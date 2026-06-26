#include "Compiler/Compiler.h"

#include "IR/IR.h"
#include "Lex/Lexer.h"
#include "Parse/Parser.h"
#include "Sema/Sema.h"

namespace rtsl {

Artifact CompilerInstance::compile_source(std::string source, CompilerInvocation invocation) {
    diagnostics_.clear();
    const auto file_id = sources_.add_buffer(std::move(invocation.source_name), std::move(source));

    Lexer lexer(sources_, diagnostics_, file_id);
    const auto tokens = lexer.lex();

    Parser parser(sources_, diagnostics_, file_id, tokens);
    auto ast = parser.parse_translation_unit();

    Sema sema(sources_, diagnostics_);
    auto semantic_module = sema.analyze(ast);
    auto ir = lower_to_ir(semantic_module);

    Artifact artifact{.kind = ArtifactKind::object};
    if (!diagnostics_.has_error() && verify_ir(ir)) {
        artifact.bytes = write_artifact(ArtifactKind::object, ir);
        artifact.strings.clear();
        artifact.structs = ir.structs;
        artifact.uniforms = ir.uniforms;
        artifact.functions = ir.functions;
        artifact.function_debug = ir.function_debug;
    }
    return artifact;
}

} // namespace rtsl
