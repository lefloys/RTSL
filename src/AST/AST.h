#pragma once

#include "Basic/SourceManager.h"

#include <string>
#include <vector>

namespace rtsl {

enum class DeclKind {
    unknown,
    import,
    function,
    struct_decl,
    uniform,
    varying,
    namespace_decl,
};

struct ParameterDecl {
    std::string type;
    std::string name;
};

struct Decl {
    DeclKind kind = DeclKind::unknown;
    std::string name;
    std::vector<ParameterDecl> parameters;
    std::string return_type;
    std::vector<std::string> body_statements;
    SourceSpan span{};
    bool exported = false;
    bool entry = false;
};

struct StructField {
    std::string type;
    std::string name;
};

struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
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
};

} // namespace rtsl
