#include "IR/IR.h"

namespace rtsl {

namespace {

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::vector<std::string> split_args(std::string_view text) {
    std::vector<std::string> args;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')' && depth > 0) --depth;
        else if (text[i] == ',' && depth == 0) {
            args.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        args.push_back(trim(text.substr(start)));
    }
    return args;
}

IRFunction::BodyOp parse_statement(std::string_view statement) {
    statement = trim(statement);
    if (statement.ends_with(";")) {
        statement.remove_suffix(1);
    }
    IRFunction::BodyOp op;
    if (statement.starts_with("return ")) {
        op.kind = IRFunction::BodyOpKind::ret;
        op.value = trim(statement.substr(7));
        return op;
    }
    const auto eq = statement.find('=');
    if (eq != std::string_view::npos) {
        const auto lhs = trim(statement.substr(0, eq));
        op.value = trim(statement.substr(eq + 1));
        if (!lhs.empty() && lhs.back() == '+') {
            op.kind = IRFunction::BodyOpKind::compound_assign;
            op.op = "+";
            op.name = trim(lhs.substr(0, lhs.size() - 1));
        } else {
            op.kind = IRFunction::BodyOpKind::assign;
            op.name = lhs;
        }
        return op;
    }
    const auto open = statement.find('(');
    const auto close = statement.rfind(')');
    if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        op.kind = IRFunction::BodyOpKind::call;
        op.callee = trim(statement.substr(0, open));
        for (auto &arg : split_args(statement.substr(open + 1, close - open - 1))) {
            if (!arg.empty()) op.args.push_back(std::move(arg));
        }
        return op;
    }
    op.kind = IRFunction::BodyOpKind::expr;
    op.value = std::string(statement);
    return op;
}

} // namespace

IRModule lower_to_ir(const SemanticModule &module) {
    IRModule ir{.source_name = module.source_name};
    ir.structs = module.structs;
    ir.uniforms = module.uniforms;
    for (const auto &symbol : module.symbols) {
        if (symbol.kind == DeclKind::function) {
            ir.functions.push_back(IRFunction{
                .name = symbol.name,
                .symbol_name = symbol.name,
                .parameters = symbol.parameters,
                .return_type = symbol.return_type,
                .entry = symbol.entry,
            });
            for (const auto &statement : symbol.body_statements) {
                ir.functions.back().body_ops.push_back(parse_statement(statement));
            }
            IRFunctionDebugInfo debug{.display_name = symbol.name};
            for (const auto &parameter : symbol.parameters) {
                debug.parameter_names.push_back(parameter.name);
            }
            ir.function_debug.push_back(std::move(debug));
        }
    }
    return ir;
}

bool verify_ir(const IRModule &) {
    return true;
}

} // namespace rtsl
