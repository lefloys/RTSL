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
    bool entry = false;
};

struct IRFunctionDebugInfo {
    std::string display_name;
    std::vector<std::string> parameter_names;
};

struct IRModule {
    std::string source_name;
    std::vector<StructDecl> structs;
    std::vector<UniformBinding> uniforms;
    std::vector<IRFunction> functions;
    std::vector<IRFunctionDebugInfo> function_debug;
};

[[nodiscard]] IRModule lower_to_ir(const SemanticModule &module);
[[nodiscard]] bool verify_ir(const IRModule &module);

} // namespace rtsl
