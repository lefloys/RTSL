#pragma once

#include "Sema/Sema.h"

#include <string>
#include <vector>

namespace rtsl {

struct IRFunction {
    std::string name;
    std::string symbol_name;
    std::vector<ParameterDecl> parameters;
    std::string return_type;
    enum class BodyOpKind : u8 { declare, assign, compound_assign, call, ret, expr } ;
    struct BodyOp {
        BodyOpKind kind = BodyOpKind::expr;
        std::string type;
        std::string name;
        std::string op;
        std::string callee;
        std::vector<std::string> args;
        std::string value;
    };
    std::vector<BodyOp> body_ops;
    // Stage of a backend entry point. User functions are StageKind::none; the
    // compiler-generated backend wrappers (vert/frag/comp) carry the stage.
    StageKind stage = StageKind::none;
    // True for compiler-synthesized ABI glue (the generated stage runtime).
    bool generated = false;
};

struct StringId {
    u32 value = 0;
};

struct IRFunctionDebugInfo {
    StringId display_name;
    std::vector<StringId> parameter_names;
};

struct IRModule {
    std::string source_name;
    std::vector<StructDecl> structs;
    std::vector<UniformBinding> uniforms;
    std::vector<StageInterface> stage_interfaces;
    std::vector<IRFunction> functions;
    std::vector<IRFunctionDebugInfo> function_debug;
};

[[nodiscard]] IRModule lower_to_ir(const SemanticModule &module, DiagnosticEngine *diagnostics = nullptr);
[[nodiscard]] bool verify_ir(const IRModule &module);

} // namespace rtsl
