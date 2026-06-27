#include "Basic/Diagnostics.h"
#include "Basic/SourceManager.h"
#include "Compiler/Compiler.h"
#include "Lex/Lexer.h"
#include "Link/Linker.h"
#include "Mangle/Mangler.h"
#include "Parse/Parser.h"
#include "Backend/GlslBackend.h"
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
    assert(text.find("layout(set = 1, binding = 0) uniform sampler2D u_6albedo_7texture_h86413326") != std::string::npos);
    assert(text.find("layout(set = 1, binding = 1) uniform ub_6albedo_4tint_hac5e2bfd") != std::string::npos);
    const std::string_view bytes(reinterpret_cast<const char *>(program.bytes.data()), program.bytes.size());
    assert(bytes.find("sample(albedo::texture") == std::string_view::npos);
    assert(bytes.find("position = mvp") == std::string_view::npos);
}

constexpr const char *kGraphicsSource = R"(
uniform { mat4 mvp; }
struct Point { vec3 position; vec2 uv; }
struct Vertex { vec4 position; vec2 uv; u32 material; }
struct Fragment { vec4 color; }
input Point {
    location(0) position;
    location(1) uv;
}
varying Vertex {
    clip position;
    smooth uv;
    flat material;
}
output Fragment {
    location(0) color;
}
entry fn vert_main(Point p) -> Vertex { return Vertex(p); }
entry fn frag_main(Vertex v) -> Fragment { return Fragment(v); }
)";

void stage_interfaces_parse_and_assign_locations() {
    CompilerInstance compiler;
    auto object = compiler.compile_source(kGraphicsSource, CompilerInvocation{.source_name = "gfx.rtsl"});
    assert(!compiler.diagnostics().has_error());
    assert(object.stage_interfaces.size() == 3);

    const StageInterface *varying = nullptr;
    for (const auto &interface : object.stage_interfaces) {
        if (interface.role == StageRole::varying) {
            varying = &interface;
        }
    }
    assert(varying);
    assert(varying->type_name == "Vertex");
    // clip position is a built-in slot and consumes no location.
    assert(varying->fields[0].name == "position");
    assert(varying->fields[0].builtin == "position");
    assert(!varying->fields[0].has_location);
    // uv/material get sequential locations starting at 0.
    assert(varying->fields[1].name == "uv" && varying->fields[1].location == 0);
    assert(varying->fields[2].name == "material" && varying->fields[2].location == 1);
}

void stage_wrappers_are_generated_for_entries() {
    CompilerInstance compiler;
    auto object = compiler.compile_source(kGraphicsSource, CompilerInvocation{.source_name = "gfx.rtsl"});
    assert(!compiler.diagnostics().has_error());

    const IRFunction *vert = nullptr;
    const IRFunction *frag = nullptr;
    bool user_entry_remains = false;
    for (const auto &function : object.functions) {
        if (function.name == "vert") vert = &function;
        if (function.name == "frag") frag = &function;
        if (function.name == "vert_main" || function.name == "frag_main") {
            user_entry_remains = user_entry_remains || function.entry;
        }
    }
    // The 4-letter wrappers exist, carry their stage, and are the entry points.
    assert(vert && vert->entry && vert->stage == StageKind::vertex && vert->generated);
    assert(frag && frag->entry && frag->stage == StageKind::fragment && frag->generated);
    // User entries are demoted to plain functions.
    assert(!user_entry_remains);

    // The fragment wrapper must not read the rasterizer-only clip position.
    const auto text = disassemble_artifact(object);
    assert(text.find("void vert()") != std::string::npos);
    assert(text.find("void frag()") != std::string::npos);
    assert(text.find("__rt_write_builtin(\"position\")") != std::string::npos);
}

void stage_data_round_trips_through_bytes() {
    CompilerInstance compiler;
    auto object = compiler.compile_source(kGraphicsSource, CompilerInvocation{.source_name = "gfx.rtsl"});
    Artifact reloaded;
    DiagnosticEngine diagnostics;
    assert(read_artifact(object.bytes, reloaded, &diagnostics));
    assert(!diagnostics.has_error());
    assert(reloaded.stage_interfaces.size() == 3);
    bool found_vert = false;
    for (const auto &function : reloaded.functions) {
        if (function.name == "vert") {
            found_vert = true;
            assert(function.stage == StageKind::vertex);
            assert(function.generated);
        }
    }
    assert(found_vert);
}

void stage_io_is_derived_from_signatures() {
    // Only the varying is declared; the vertex input (Point) and fragment output
    // (Fragment) must be derived automatically from the entry signatures.
    constexpr const char *source = R"(
struct Point { vec3 position; vec2 uv; }
struct Vertex { vec4 position; vec2 uv; }
struct Fragment { vec4 color; }
varying Vertex { clip position; smooth uv; }
entry fn vert_main(Point p) -> Vertex { return Vertex(p); }
entry fn frag_main(Vertex v) -> Fragment { return Fragment(v); }
)";
    rtsl_context ctx = rtslCreateContext();
    rtsl_module object = rtslCompileSource(ctx, source, std::strlen(source), "derive.rtsl");
    assert(object);

    // Derived input Point and output Fragment locations are queryable by name.
    uint32_t location = 99;
    assert(rtslModuleGetStageLocation(object, "Point", "position", &location) && location == 0);
    assert(rtslModuleGetStageLocation(object, "Point", "uv", &location) && location == 1);
    assert(rtslModuleGetStageLocation(object, "Fragment", "color", &location) && location == 0);
    // The varying's interpolated field keeps its own location; clip position is a
    // built-in slot with no location.
    assert(rtslModuleGetStageLocation(object, "Vertex", "uv", &location) && location == 0);
    assert(!rtslModuleGetStageLocation(object, "Vertex", "position", &location));
    assert(!rtslModuleGetStageLocation(object, "Point", "missing", &location));

    rtslDestroyModule(object);
    rtslDestroyContext(ctx);
}

void rasterized_payload_requires_varying() {
    // A vertex output with no varying declaration cannot be interpolated.
    CompilerInstance compiler;
    auto object = compiler.compile_source(
        "struct Point { vec3 p; }\nstruct Vertex { vec4 position; }\n"
        "entry fn vert_main(Point p) -> Vertex { return Vertex(p); }",
        CompilerInvocation{.source_name = "novarying.rtsl"});
    assert(compiler.diagnostics().has_error());
    (void)object;
}

void graphics_program_requires_vertex_and_fragment() {
    // A graphics program with only a vertex stage must fail validation.
    CompilerInstance compiler;
    auto object = compiler.compile_source(
        "struct Point { vec3 p; }\nstruct Vertex { vec4 position; }\n"
        "varying Vertex { clip position; }\n"
        "entry fn vert_main(Point p) -> Vertex { return Vertex(p); }",
        CompilerInvocation{.source_name = "vertonly.rtsl"});
    assert(!compiler.diagnostics().has_error());
    Linker linker(compiler.diagnostics());
    assert(linker.add_artifact(object));
    auto program = linker.link_program();
    assert(compiler.diagnostics().has_error()); // missing fragment stage
}

void reflection_exposes_uniforms_and_stage_io() {
    rtsl_context ctx = rtslCreateContext();
    assert(ctx);
    rtsl_module object = rtslCompileSource(ctx, kGraphicsSource, std::strlen(kGraphicsSource), "gfx.rtsl");
    assert(object);

    // Uniforms are queryable from outside the compiler.
    assert(rtslModuleGetUniformCount(object) == 1);
    rtsl_uniform_info uniform{};
    assert(rtslModuleGetUniform(object, 0, &uniform));
    assert(std::strcmp(uniform.name, "mvp") == 0);
    assert(uniform.set == 0 && uniform.binding == 0);
    assert(std::strlen(uniform.binding_name) > 0);

    // Stage variables are queryable with their assigned locations.
    const size_t var_count = rtslModuleGetStageVariableCount(object);
    assert(var_count > 0);
    bool saw_uv_location = false;
    for (size_t i = 0; i < var_count; ++i) {
        rtsl_stage_variable var{};
        assert(rtslModuleGetStageVariable(object, i, &var));
        if (std::strcmp(var.payload_type, "Vertex") == 0 && std::strcmp(var.name, "uv") == 0) {
            assert(var.role == RTSL_STAGE_ROLE_VARYING);
            assert(var.has_location && var.location == 0);
            saw_uv_location = true;
        }
    }
    assert(saw_uv_location);

    // Entries reflect the generated 4-letter stage entry points.
    bool saw_vert = false;
    bool saw_frag = false;
    const size_t entry_count = rtslModuleGetEntryCount(object);
    for (size_t i = 0; i < entry_count; ++i) {
        rtsl_entry_info entry{};
        assert(rtslModuleGetEntry(object, i, &entry));
        if (std::strcmp(entry.name, "vert") == 0 && entry.stage == RTSL_STAGE_VERTEX) saw_vert = true;
        if (std::strcmp(entry.name, "frag") == 0 && entry.stage == RTSL_STAGE_FRAGMENT) saw_frag = true;
    }
    assert(saw_vert && saw_frag);

    rtslDestroyModule(object);
    rtslDestroyContext(ctx);
}

void vulkan_glsl_splits_into_per_stage_files() {
    CompilerInstance compiler;
    auto object = compiler.compile_source(kGraphicsSource, CompilerInvocation{.source_name = "gfx.rtsl"});
    Linker linker(compiler.diagnostics());
    assert(linker.add_artifact(object));
    auto program = linker.link_program();
    assert(!compiler.diagnostics().has_error());

    const auto files = emit_vulkan_glsl(program);
    const GlslStageFile *vert = nullptr;
    const GlslStageFile *frag = nullptr;
    for (const auto &file : files) {
        if (file.stage == StageKind::vertex) vert = &file;
        if (file.stage == StageKind::fragment) frag = &file;
    }
    // RTSL keeps both stages in one module; GLSL emission must split them.
    assert(vert && frag);
    assert(vert->extension == ".vert" && frag->extension == ".frag");

    // Each file is a standalone Vulkan GLSL translation unit.
    assert(vert->source.find("#version 450") == 0);
    assert(frag->source.find("#version 450") == 0);

    // The vertex stage reads its attributes and writes the clip position + varyings.
    assert(vert->source.find("layout(location = 0) in vec3 in_position;") != std::string::npos);
    assert(vert->source.find("gl_Position = _out.position;") != std::string::npos);
    assert(vert->source.find("out_uv = _out.uv;") != std::string::npos);

    // The fragment stage reads the interpolated varyings (not the clip position)
    // and writes the framebuffer output.
    assert(frag->source.find("flat in uint in_material;") != std::string::npos);
    assert(frag->source.find("in_position") == std::string::npos); // clip position is not a fragment input
    assert(frag->source.find("layout(location = 0) out vec4 out_color;") != std::string::npos);
    assert(frag->source.find("out_color = _out.color;") != std::string::npos);
}

void glsl_rewrites_constructors_and_carrier_builtins() {
    // gl_Position is written through the builtin carrier (RtVertex&) passed by
    // reference as the first parameter, and the entries use constructor-call
    // syntax that GLSL does not allow.
    constexpr const char *source = R"(
uniform { mat4 mvp; }
struct Point { vec3 position; vec2 uv; }
struct Vertex { vec2 uv; u32 material; }
struct Fragment { vec4 color; }
varying Vertex { smooth uv; flat material; }
fn Vertex::Vertex(Point p) { uv = p.uv; material = 0; }
fn Fragment::Fragment(Vertex v) { color = vec4(v.uv, 0.0, 1.0); }
entry fn vert_main(RtVertex& b, Point p) -> Vertex {
    b.position = mvp * vec4(p.position, 1.0);
    return Vertex(p);
}
entry fn frag_main(Vertex v) -> Fragment { return Fragment(v); }
)";
    CompilerInstance compiler;
    auto object = compiler.compile_source(source, CompilerInvocation{.source_name = "carrier.rtsl"});
    assert(!compiler.diagnostics().has_error());
    Linker linker(compiler.diagnostics());
    assert(linker.add_artifact(object));
    auto program = linker.link_program();
    assert(!compiler.diagnostics().has_error());

    const auto files = emit_vulkan_glsl(program);
    const GlslStageFile *vert = nullptr;
    for (const auto &file : files) {
        if (file.stage == StageKind::vertex) vert = &file;
    }
    assert(vert);

    // The carrier struct is emitted and passed by inout reference.
    assert(vert->source.find("struct RtVertex {") != std::string::npos);
    assert(vert->source.find("inout RtVertex b") != std::string::npos);
    // Constructor-call syntax is rewritten to the real constructor function.
    assert(vert->source.find("return _ZN6Vertex6VertexE5Point(p);") != std::string::npos);
    assert(vert->source.find("return Vertex(p);") == std::string::npos);
    // main() copies the carrier's position out to the gl_Position global.
    assert(vert->source.find("gl_Position = _bi.position;") != std::string::npos);
    // The carrier call forwards the carrier by reference, payload by value.
    assert(vert->source.find("(_bi, _in)") != std::string::npos);
    // position is a builtin (carrier), never an interpolated varying output.
    assert(vert->source.find("out out_position") == std::string::npos);
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
    stage_interfaces_parse_and_assign_locations();
    stage_wrappers_are_generated_for_entries();
    stage_data_round_trips_through_bytes();
    stage_io_is_derived_from_signatures();
    rasterized_payload_requires_varying();
    graphics_program_requires_vertex_and_fragment();
    reflection_exposes_uniforms_and_stage_io();
    vulkan_glsl_splits_into_per_stage_files();
    glsl_rewrites_constructors_and_carrier_builtins();
    linker_emits_program();
    c_abi_lifetime_and_errors();
    std::cout << "rtsl-tests passed\n";
    return 0;
}
