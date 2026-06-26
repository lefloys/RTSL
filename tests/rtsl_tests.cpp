#include "Basic/Diagnostics.h"
#include "Basic/SourceManager.h"
#include "Compiler/Compiler.h"
#include "Lex/Lexer.h"
#include "Link/Linker.h"
#include "Mangle/Mangler.h"
#include "Parse/Parser.h"
#include "Serialization/Artifact.h"
#include "Serialization/TextRTIR.h"
#include "rtsl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string_view>

using namespace rtsl;

namespace {

void source_locations_are_line_column_mapped() {
    SourceManager sources;
    const auto file = sources.add_buffer("test.rtsl", "a\nbc\n");
    const auto loc = sources.location_at(file, 3);
    assert(loc.line == 2);
    assert(loc.column == 2);
}

void diagnostics_store_errors() {
    DiagnosticEngine diagnostics;
    diagnostics.report(1, DiagnosticSeverity::error, {}, "x", "broken");
    assert(diagnostics.has_error());
    assert(diagnostics.diagnostics().front().message == "broken");
}

void lexer_tokenizes_keywords_and_punctuation() {
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto file = sources.add_buffer("test.rtsl", "entry fn main() -> void {}");
    Lexer lexer(sources, diagnostics, file);
    const auto tokens = lexer.lex();
    assert(!diagnostics.has_error());
    assert(tokens[0].kind == TokenKind::kw_Entry);
    assert(tokens[1].kind == TokenKind::kw_Function);
    assert(tokens[2].kind == TokenKind::identifier);
    assert(tokens[5].kind == TokenKind::arrow);
}

void parser_builds_translation_unit() {
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto file = sources.add_buffer("test.rtsl", "entry fn main() {}");
    Lexer lexer(sources, diagnostics, file);
    const auto tokens = lexer.lex();
    Parser parser(sources, diagnostics, file, tokens);
    const auto unit = parser.parse_translation_unit();
    assert(!diagnostics.has_error());
    assert(unit.declarations.size() == 1);
    assert(unit.declarations.front().entry);
}

void artifact_round_trips() {
    IRModule module{.source_name = "test.rtsl", .functions = {IRFunction{.name = "main", .entry = true}}};
    auto bytes = write_artifact(ArtifactKind::program, module);
    Artifact artifact;
    DiagnosticEngine diagnostics;
    assert(read_artifact(bytes, artifact, &diagnostics));
    assert(!diagnostics.has_error());
    assert(artifact.kind == ArtifactKind::program);
    assert(artifact.strings.size() == 3);
    assert(artifact.functions.size() == 1);
    assert(artifact.functions.front().name == "main");
}

void text_rtir_round_trips_minimal_artifact() {
    IRModule module{.source_name = "test.rtsl", .functions = {IRFunction{.name = "main", .entry = true}}};
    Artifact artifact;
    assert(read_artifact(write_artifact(ArtifactKind::program, module), artifact));

    const auto text = disassemble_artifact(artifact);
    assert(text.find("strings") == std::string::npos);
    assert(text.find("entry ") == std::string::npos);
    assert(text.find("void main();") != std::string::npos);
    Artifact assembled;
    DiagnosticEngine diagnostics;
    if (!assemble_text_rtir(text, assembled, &diagnostics)) {
        for (const auto &diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.message << '\n';
        }
        assert(false);
    }
    assert(!diagnostics.has_error());
    assert(assembled.kind == ArtifactKind::program);
    assert(assembled.strings.size() == 3);
    assert(assembled.functions.size() == 1);
    assert(assembled.functions.front().return_type == "void");
}

void mangler_keeps_readable_and_glsl_modes_separate() {
    const IRFunction function{
        .name = "Vertex::Vertex",
        .symbol_name = "Vertex::Vertex",
        .parameters = {ParameterDecl{.type = "Point"}},
        .return_type = "void",
    };
    const Mangler mangler;
    assert(mangler.mangle_rtsl(function) == "_ZN6Vertex6VertexE5Point");
    assert(mangler.mangle_for_glsl(function) == "___ZN6Vertex6VertexE5Point");
}

void compiler_emits_object() {
    CompilerInstance compiler;
    auto artifact = compiler.compile_source("entry fn main(Point p) -> Vertex { return Vertex(p); }", CompilerInvocation{.source_name = "test.rtsl"});
    assert(!compiler.diagnostics().has_error());
    assert(artifact.kind == ArtifactKind::object);
    assert(!artifact.bytes.empty());
    assert(artifact.functions.size() == 1);
    assert(artifact.functions.front().parameters.size() == 1);
    assert(artifact.functions.front().parameters.front().type == "Point");
    assert(artifact.functions.front().parameters.front().name.empty());
    assert(artifact.function_debug.front().parameter_names.front() == "p");
    assert(artifact.functions.front().return_type == "Vertex");
    assert(!artifact.functions.front().body_ops.empty());
    const std::string_view bytes(reinterpret_cast<const char *>(artifact.bytes.data()), artifact.bytes.size());
    assert(bytes.find("return Vertex") == std::string_view::npos);
}

void uniforms_lower_to_separate_named_bindings() {
    constexpr const char *source = R"(
uniform {
    mat4 mvp;
}
uniform albedo {
    Sampler2D texture;
    vec4 tint;
}
entry fn main() {
    color = sample(albedo::texture, uv) * albedo::tint;
    position = mvp * position;
}
)";
    CompilerInstance compiler;
    auto object = compiler.compile_source(source, CompilerInvocation{.source_name = "uniforms.rtsl"});
    Linker linker(compiler.diagnostics());
    assert(linker.add_artifact(object));
    auto program = linker.link_program();
    assert(!compiler.diagnostics().has_error());

    const auto text = disassemble_artifact(program);
    assert(text.find("layout(set = 0, binding = 0) uniform ub_3mvp_h974f4b00") != std::string::npos);
    assert(text.find("layout(set = 1, binding = 0) uniform sampler2D u_6albedo_7texture_h0b44431c") != std::string::npos);
    assert(text.find("layout(set = 1, binding = 1) uniform ub_6albedo_4tint_h93604339") != std::string::npos);
    const std::string_view bytes(reinterpret_cast<const char *>(program.bytes.data()), program.bytes.size());
    assert(bytes.find("sample(albedo::texture") == std::string_view::npos);
    assert(bytes.find("position = mvp") == std::string_view::npos);
}

void linker_emits_program() {
    CompilerInstance compiler;
    auto object = compiler.compile_source("entry fn main() {}", CompilerInvocation{.source_name = "test.rtsl"});
    Linker linker(compiler.diagnostics());
    assert(linker.add_artifact(object));
    auto program = linker.link_program();
    assert(!compiler.diagnostics().has_error());
    assert(program.kind == ArtifactKind::program);
    assert(!program.bytes.empty());
}

void c_abi_lifetime_and_errors() {
    rtsl_context ctx = rtslCreateContext();
    assert(ctx);
    const char *source = "entry fn main() {}";
    rtsl_module object = rtslCompileSource(ctx, source, std::strlen(source), "abi.rtsl");
    assert(object);
    assert(rtslModuleGetBytecode(object).size > 0);

    rtsl_linker linker = rtslCreateLinker(ctx);
    assert(linker);
    assert(rtslLinkerAddModule(linker, object));
    rtsl_module program = rtslLinkProgram(linker);
    assert(program);
    assert(rtslModuleGetKind(program) == RTSL_OUTPUT_PROGRAM);

    rtslDestroyModule(program);
    rtslDestroyLinker(linker);
    rtslDestroyModule(object);
    rtslDestroyContext(ctx);
}

} // namespace

int main() {
    source_locations_are_line_column_mapped();
    diagnostics_store_errors();
    lexer_tokenizes_keywords_and_punctuation();
    parser_builds_translation_unit();
    artifact_round_trips();
    text_rtir_round_trips_minimal_artifact();
    mangler_keeps_readable_and_glsl_modes_separate();
    compiler_emits_object();
    uniforms_lower_to_separate_named_bindings();
    linker_emits_program();
    c_abi_lifetime_and_errors();
    std::cout << "rtsl-tests passed\n";
    return 0;
}
