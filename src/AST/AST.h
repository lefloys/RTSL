#pragma once

#include "Basic/SourceManager.h"

#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

enum class DeclKind {
    unknown,
    import,
    function,
    struct_decl,
    uniform,
    varying,
    input,
    output,
    namespace_decl,
};

// Backend shader stage. The 4-letter spellings (vert/frag/comp) are the names
// of the compiler-generated backend entry points for each stage.
enum class StageKind : u8 {
    none,
    vertex,
    fragment,
    compute,
};

[[nodiscard]] inline std::string_view stage_entry_name(StageKind stage) {
    switch (stage) {
    case StageKind::vertex: return "vert";
    case StageKind::fragment: return "frag";
    case StageKind::compute: return "comp";
    case StageKind::none: return "";
    }
    return "";
}

enum class StageRole : u8 {
    input,
    varying,
    output,
};

// One field of a stage interface payload, with its ABI placement.
struct StageIOField {
    std::string name;
    std::string interpolation; // "smooth" | "flat" | "clip" | ""
    std::string builtin;       // builtin slot name, or "" for a user location
    u32 location = 0;
    bool has_location = false;
};

// A declared stage interface: how a payload struct's fields cross a stage
// boundary (input attributes, interpolated varyings, or stage outputs).
struct StageInterface {
    StageRole role = StageRole::varying;
    std::string type_name;
    std::vector<StageIOField> fields;
};

struct ParameterDecl {
    std::string type;
    std::string name;
    bool is_const = false;
    bool is_reference = false;
};

struct Decl {
    DeclKind kind = DeclKind::unknown;
    std::string name;
    std::vector<ParameterDecl> parameters;
    std::string return_type;
    std::vector<std::string> body_statements;
    SourceSpan span{};
    bool exported = false;
};

struct StructField {
    std::string type;
    std::string name;
};

struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
    std::vector<ParameterDecl> constructor_parameters;
};

struct UniformBinding {
    std::string scope_name;
    std::string name;
    std::string type;
    std::vector<StructField> inline_fields;
    std::string access;
    u32 set = 0;
    u32 binding = 0;
};

struct TranslationUnit {
    u32 file_id = 0;
    std::vector<Decl> declarations;
    std::vector<StructDecl> structs;
    std::vector<UniformBinding> uniforms;
    std::vector<StageInterface> stage_interfaces;
};

} // namespace rtsl
