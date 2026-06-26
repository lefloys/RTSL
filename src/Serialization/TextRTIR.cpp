#include "Serialization/TextRTIR.h"

#include "IR/IR.h"
#include "Mangle/Mangler.h"

#include <cctype>
#include <sstream>
#include <string>

namespace rtsl {

namespace {

std::string_view kind_name(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::object: return "rtslo";
    case ArtifactKind::module: return "rtslm";
    case ArtifactKind::library: return "rtsll";
    case ArtifactKind::program: return "rtslp";
    }
    return "unknown";
}

bool parse_kind(std::string_view text, ArtifactKind &kind) {
    if (text == "rtslo") {
        kind = ArtifactKind::object;
        return true;
    }
    if (text == "rtslm") {
        kind = ArtifactKind::module;
        return true;
    }
    if (text == "rtsll") {
        kind = ArtifactKind::library;
        return true;
    }
    if (text == "rtslp") {
        kind = ArtifactKind::program;
        return true;
    }
    return false;
}

std::string unquote(std::string_view text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text.remove_prefix(1);
        text.remove_suffix(1);
    }

    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
        }
        result.push_back(text[i]);
    }
    return result;
}

std::string quote(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

void report(DiagnosticEngine *diagnostics, std::string message) {
    if (diagnostics) {
        diagnostics->report(7001, DiagnosticSeverity::error, {}, "<rtir>", std::move(message));
    }
}

std::string lower_function_name(std::string_view name) {
    std::string lowered;
    lowered.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            lowered += "__";
            ++i;
        } else {
            lowered.push_back(name[i]);
        }
    }
    return lowered;
}

bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string sanitize_symbol_part(std::string_view part) {
    std::string sanitized;
    sanitized.reserve(part.size());
    for (const char c : part) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            sanitized.push_back(c);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

void append_mangled_part(std::string &name, std::string_view part) {
    const auto sanitized = sanitize_symbol_part(part);
    name += std::to_string(sanitized.size());
    name += sanitized;
}

u32 stable_uniform_hash(const UniformBinding &uniform) {
    u32 hash = 2166136261u;
    const auto add = [&](std::string_view text) {
        for (const unsigned char c : text) {
            hash ^= c;
            hash *= 16777619u;
        }
    };
    add(uniform.scope_name);
    add("\0");
    add(uniform.name);
    add("\0");
    add(uniform.type);
    add("\0");
    add(std::to_string(uniform.set));
    add("\0");
    add(std::to_string(uniform.binding));
    return hash;
}

std::string hash_suffix(u32 hash) {
    constexpr char Digits[] = "0123456789abcdef";
    std::string out = "_h";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.push_back(Digits[(hash >> shift) & 0xfu]);
    }
    return out;
}

std::string uniform_binding_name(const UniformBinding &uniform) {
    std::string name = "u_";
    if (!uniform.scope_name.empty()) {
        append_mangled_part(name, uniform.scope_name);
        name += "_";
    }
    append_mangled_part(name, uniform.name);
    name += hash_suffix(stable_uniform_hash(uniform));
    return name;
}

std::string uniform_block_name(const UniformBinding &uniform) {
    std::string name = "ub_";
    if (!uniform.scope_name.empty()) {
        append_mangled_part(name, uniform.scope_name);
        name += "_";
    }
    append_mangled_part(name, uniform.name);
    name += hash_suffix(stable_uniform_hash(uniform));
    return name;
}

bool is_resource_uniform_type(std::string_view type) {
    return type == "Sampler2D" || type.starts_with("Buffer<");
}

bool is_replacement_boundary(std::string_view text, std::size_t pos) {
    if (pos >= text.size()) {
        return true;
    }
    const char c = text[pos];
    return !is_identifier_char(c) && c != ':';
}

std::string replace_symbol(std::string text, std::string_view from, std::string_view to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        if ((pos != 0 && !is_replacement_boundary(text, pos - 1)) ||
            !is_replacement_boundary(text, pos + from.size())) {
            pos += from.size();
            continue;
        }
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string lower_uniform_references(std::string statement, const std::vector<UniformBinding> &uniforms) {
    for (const auto &uniform : uniforms) {
        if (uniform.scope_name.empty()) {
            statement = replace_symbol(std::move(statement), uniform.name, uniform_binding_name(uniform) + ".value");
            continue;
        }
        const auto source_name = uniform.scope_name + "::" + uniform.name;
        const auto lowered_name = is_resource_uniform_type(uniform.type)
                                      ? uniform_binding_name(uniform)
                                      : uniform_binding_name(uniform) + ".value";
        statement = replace_symbol(std::move(statement), source_name, lowered_name);
    }
    return statement;
}

std::string lower_uniform_type(std::string_view type) {
    if (type == "Sampler2D") {
        return "sampler2D";
    }
    return std::string(type);
}

std::string member_owner(std::string_view name) {
    const auto scope = name.find("::");
    if (scope == std::string_view::npos) {
        return {};
    }
    return std::string(name.substr(0, scope));
}

bool is_constructor(const IRFunction &function) {
    const auto owner = member_owner(function.name);
    if (owner.empty()) {
        return false;
    }
    const auto scope = function.name.find("::");
    return scope != std::string::npos && function.name.substr(scope + 2) == owner;
}

std::string lower_member_statement(std::string_view statement, std::string_view owner) {
    if (owner.empty() || statement.empty() || !((statement.front() >= 'a' && statement.front() <= 'z') ||
                                                (statement.front() >= 'A' && statement.front() <= 'Z') ||
                                                statement.front() == '_')) {
        return std::string(statement);
    }

    std::size_t ident_end = 1;
    while (ident_end < statement.size() && is_identifier_char(statement[ident_end])) {
        ++ident_end;
    }

    const auto rest = statement.substr(ident_end);
    if (rest.starts_with("=") || rest.starts_with("+=") || rest.starts_with("-=") ||
        rest.starts_with("*=") || rest.starts_with("/=") || rest.starts_with("%=")) {
        std::string lowered = "this.";
        lowered += statement;
        return lowered;
    }
    return std::string(statement);
}

std::string format_statement(std::string_view statement) {
    std::string out;
    out.reserve(statement.size() + 8);
    int angle_depth = 0;
    for (std::size_t i = 0; i < statement.size(); ++i) {
        const char c = statement[i];
        if (c == '<') {
            ++angle_depth;
        } else if (c == '>' && angle_depth > 0) {
            --angle_depth;
        }

        if (c == ',' && angle_depth > 0) {
            out += ", ";
            continue;
        }

        if (c == '>' && i + 1 < statement.size()) {
            const char next = statement[i + 1];
            out.push_back(c);
            if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next == '_') {
                out.push_back(' ');
            }
            continue;
        }

        out.push_back(c);
    }
    return out;
}

const IRFunction *find_constructor(std::string_view type_name, const std::vector<IRFunction> &functions) {
    const std::string constructor_name = std::string(type_name) + "::" + std::string(type_name);
    for (const auto &function : functions) {
        if (function.name == constructor_name) {
            return &function;
        }
    }
    return nullptr;
}

std::string render_body_op(const IRFunction::BodyOp &op, const std::vector<IRFunction> &functions, const Mangler &mangler) {
    switch (op.kind) {
    case IRFunction::BodyOpKind::ret:
        return "return " + op.value + ";";
    case IRFunction::BodyOpKind::assign:
        return op.name + " = " + op.value + ";";
    case IRFunction::BodyOpKind::compound_assign:
        return op.name + " " + op.op + "= " + op.value + ";";
    case IRFunction::BodyOpKind::call: {
        const auto *constructor = find_constructor(op.callee, functions);
        std::string callee = constructor ? lower_function_name(mangler.mangle_rtsl(*constructor)) : op.callee;
        std::string out = callee + "(";
        for (std::size_t i = 0; i < op.args.size(); ++i) {
            if (i) out += ", ";
            out += op.args[i];
        }
        out += ");";
        return out;
    }
    default:
        return op.value + ";";
    }
}

} // namespace

std::string disassemble_artifact(const Artifact &artifact) {
    std::ostringstream out;
    out << "artifact " << kind_name(artifact.kind) << " 1.0\n\n";

    out << "source " << quote(artifact.strings.empty() ? "<unknown>" : artifact.strings.front()) << "\n\n";
    for (const auto &struct_decl : artifact.structs) {
        out << "struct " << struct_decl.name << " {\n";
        for (const auto &field : struct_decl.fields) {
            out << "  " << field.type << " " << field.name << ";\n";
        }
        out << "};\n\n";
    }
    for (const auto &uniform : artifact.uniforms) {
        const auto binding_name = uniform_binding_name(uniform);
        out << "layout(set = " << uniform.set << ", binding = " << uniform.binding << ") ";
        if (is_resource_uniform_type(uniform.type)) {
            if (!uniform.access.empty()) {
                out << uniform.access << " ";
            }
            out << "uniform " << lower_uniform_type(uniform.type) << " " << binding_name << ";\n\n";
        } else {
            out << "uniform " << uniform_block_name(uniform) << " {\n";
            if (!uniform.inline_fields.empty()) {
                for (const auto &field : uniform.inline_fields) {
                    out << "  " << field.type << " " << field.name << ";\n";
                }
            } else {
                out << "  " << uniform.type << " value;\n";
            }
            out << "} " << binding_name << ";\n\n";
        }
    }
    if (!artifact.functions.empty()) {
        const Mangler mangler;
        for (std::size_t function_index = 0; function_index < artifact.functions.size(); ++function_index) {
            const auto &function = artifact.functions[function_index];
            const auto *debug = function_index < artifact.function_debug.size() ? &artifact.function_debug[function_index] : nullptr;
            const auto disassembly_name = lower_function_name(mangler.mangle_rtsl(function));
            const auto owner = member_owner(function.name);
            const bool constructor = is_constructor(function);
            out << (constructor ? owner : (function.return_type.empty() ? "void" : function.return_type)) << " "
                << disassembly_name << "(";
            bool needs_comma = false;
            if (!owner.empty() && !constructor) {
                out << "inout " << owner << " this";
                needs_comma = true;
            }
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (needs_comma || i != 0) {
                    out << ", ";
                }
                out << function.parameters[i].type;
                const std::string parameter_name =
                    debug && i < debug->parameter_names.size() ? debug->parameter_names[i] : function.parameters[i].name;
                if (!parameter_name.empty()) {
                    out << " " << parameter_name;
                } else {
                    out << " p" << i;
                }
                needs_comma = true;
            }
            if (function.body_ops.empty()) {
                out << ");\n";
            } else {
                out << ") {\n";
                if (constructor) {
                    out << "  " << owner << " this;\n";
                }
                for (const auto &op : function.body_ops) {
                    auto rendered = render_body_op(op, artifact.functions, mangler);
                    out << "  " << lower_uniform_references(std::move(rendered), artifact.uniforms) << "\n";
                }
                if (constructor) {
                    out << "  return this;\n";
                }
                out << "}\n";
            }
        }
    } else {
        out << "// no functions\n";
    }
    return out.str();
}

bool assemble_text_rtir(std::string_view text, Artifact &artifact, DiagnosticEngine *diagnostics) {
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line)) {
        report(diagnostics, "expected 'artifact <kind> 1.0'");
        return false;
    }

    std::istringstream header(line);
    std::string word;
    std::string kind_word;
    std::string version;
    if (!(header >> word >> kind_word >> version) || word != "artifact" || version != "1.0") {
        report(diagnostics, "expected 'artifact <kind> 1.0'");
        return false;
    }

    ArtifactKind kind{};
    if (!parse_kind(kind_word, kind)) {
        report(diagnostics, "unknown artifact kind");
        return false;
    }

    IRModule module;
    while (std::getline(input, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.starts_with("//")) {
            continue;
        }

        if (trimmed.starts_with("source ")) {
            module.source_name = unquote(std::string_view(trimmed).substr(7));
            continue;
        }

        bool entry = false;
        std::string_view declaration = trimmed;
        if (declaration.starts_with("entry ")) {
            entry = true;
            declaration.remove_prefix(6);
        }
        if (declaration.ends_with(";")) {
            declaration.remove_suffix(1);
            const auto first_space = declaration.find(' ');
            if (first_space == std::string_view::npos) {
                report(diagnostics, "expected GLSL-style function declaration");
                return false;
            }
            std::string return_type = trim(declaration.substr(0, first_space));
            declaration.remove_prefix(first_space + 1);
            const auto open_paren = declaration.find('(');
            const auto close_paren = declaration.find(')', open_paren == std::string_view::npos ? 0 : open_paren);
            if (open_paren == std::string_view::npos || close_paren == std::string_view::npos) {
                report(diagnostics, "expected function declaration");
                return false;
            }
            auto name = trim(declaration.substr(0, open_paren));
            auto params_text = declaration.substr(open_paren + 1, close_paren - open_paren - 1);
            std::vector<ParameterDecl> parameters;
            while (!params_text.empty()) {
                const auto comma = params_text.find(',');
                const auto part = comma == std::string_view::npos ? params_text : params_text.substr(0, comma);
                auto param = trim(part);
                if (!param.empty()) {
                    const auto last_space = param.rfind(' ');
                    if (last_space == std::string::npos) {
                        parameters.push_back(ParameterDecl{.type = std::move(param)});
                    } else {
                        parameters.push_back(ParameterDecl{
                            .type = param.substr(0, last_space),
                            .name = param.substr(last_space + 1),
                        });
                    }
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                params_text.remove_prefix(comma + 1);
            }
            module.functions.push_back(IRFunction{
                .name = std::move(name),
                .symbol_name = {},
                .parameters = std::move(parameters),
                .return_type = std::move(return_type),
                .entry = entry,
            });
            IRFunctionDebugInfo debug;
            debug.display_name = module.functions.back().name;
            for (const auto &parameter : module.functions.back().parameters) {
                debug.parameter_names.push_back(parameter.name);
            }
            module.function_debug.push_back(std::move(debug));
            continue;
        }

        report(diagnostics, "expected source or function declaration");
        return false;
    }
    if (module.source_name.empty()) {
        module.source_name = "<assembled>";
    }

    artifact.kind = kind;
    artifact.bytes = write_artifact(kind, module);
    Artifact parsed;
    if (!read_artifact(artifact.bytes, parsed, diagnostics)) {
        return false;
    }
    parsed.bytes = std::move(artifact.bytes);
    artifact = std::move(parsed);
    return true;
}

} // namespace rtsl
