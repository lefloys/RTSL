#include "IR/IR.h"

#include "IR/StageBuiltins.h"
#include "IR/UniformLowering.h"
#include "Mangle/Mangler.h"

#include <algorithm>

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

IRFunction::BodyOp parse_statement(std::string_view raw_statement) {
    // Hold the trimmed text in an owned string so the string_view below cannot
    // dangle into a destroyed temporary.
    const std::string owned = trim(raw_statement);
    std::string_view statement = owned;
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
        // A trailing arithmetic operator on the lhs marks a compound assignment
        // (+=, -=, *=, /=, %=).
        if (!lhs.empty() && (lhs.back() == '+' || lhs.back() == '-' || lhs.back() == '*' ||
                             lhs.back() == '/' || lhs.back() == '%')) {
            op.kind = IRFunction::BodyOpKind::compound_assign;
            op.op = std::string(1, lhs.back());
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

// Infer the backend stage of an entry function from its name. The convention is
// that an entry's name begins with the stage's 4-letter tag (vert/frag/comp).
StageKind detect_stage(std::string_view name) {
    if (name.starts_with("vert")) return StageKind::vertex;
    if (name.starts_with("frag")) return StageKind::fragment;
    if (name.starts_with("comp")) return StageKind::compute;
    return StageKind::none;
}

const StageInterface *find_interface(const std::vector<StageInterface> &interfaces, std::string_view type_name) {
    for (const auto &interface : interfaces) {
        if (interface.type_name == type_name) {
            return &interface;
        }
    }
    return nullptr;
}

// A clip-space position only travels vertex -> rasterizer; it is not a readable
// input on later stages.
bool is_rasterizer_only(const StageIOField &field) {
    return field.interpolation == "clip" || field.builtin == "position";
}

std::string globals_type_name(StageKind stage) {
    switch (stage) {
    case StageKind::vertex: return "vert_globals";
    case StageKind::fragment: return "frag_globals";
    case StageKind::compute: return "comp_globals";
    case StageKind::none: return "globals";
    }
    return "globals";
}

bool is_stage_globals_type(StageKind stage, std::string_view type) {
    return type == globals_type_name(stage);
}

std::string read_intrinsic(const StageIOField &field) {
    if (!field.builtin.empty()) {
        return "__rt_read_builtin(\"" + field.builtin + "\")";
    }
    return "__rt_read_input(" + std::to_string(field.location) + ")";
}

std::string write_target(const StageIOField &field) {
    if (!field.builtin.empty()) {
        return "__rt_write_builtin(\"" + field.builtin + "\")";
    }
    return "__rt_write_output(" + std::to_string(field.location) + ")";
}

bool is_member_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Whether a function body reads or writes `.member` anywhere, so the generated
// runtime only copies builtins the shader actually touches.
bool body_uses_member(const IRFunction &function, const std::string &member) {
    const std::string needle = "." + member;
    const auto hit = [&](const std::string &text) {
        std::size_t pos = text.find(needle);
        while (pos != std::string::npos) {
            const std::size_t after = pos + needle.size();
            if (after >= text.size() || !is_member_char(text[after])) {
                return true;
            }
            pos = text.find(needle, pos + 1);
        }
        return false;
    };
    for (const auto &op : function.body_ops) {
        if (hit(op.name) || hit(op.value)) return true;
        for (const auto &arg : op.args) {
            if (hit(arg)) return true;
        }
    }
    return false;
}

// Build the compiler-generated backend entry wrapper for one user entry. The
// wrapper copies the builtin carrier's inputs in, reads the stage input payload,
// calls the user entry, then copies builtin outputs and the payload back out.
IRFunction make_stage_wrapper(StageKind stage, const IRFunction &user, const std::string &callee,
                              const std::string &carrier_type, const std::string &input_type,
                              const std::string &output_type, const std::vector<StageInterface> &interfaces) {
    IRFunction wrapper{
        .name = std::string(stage_entry_name(stage)),
        .symbol_name = std::string(stage_entry_name(stage)),
        .return_type = "void",
        .stage = stage,
        .generated = true,
    };

    const auto builtins = stage_builtins(carrier_type);

    // Copy-in: read the real gl_* input builtins the shader uses into the carrier.
    for (const auto &builtin : builtins) {
        if (!builtin.is_output && body_uses_member(user, builtin.member)) {
            wrapper.body_ops.push_back(IRFunction::BodyOp{
                .kind = IRFunction::BodyOpKind::assign,
                .name = "bi." + builtin.member,
                .value = "__rt_read_builtin(\"" + builtin.gl_name + "\")",
            });
        }
    }

    // Read the stage input payload.
    const bool has_input = !input_type.empty();
    if (has_input) {
        if (const auto *in_interface = find_interface(interfaces, input_type)) {
            for (const auto &field : in_interface->fields) {
                if (is_rasterizer_only(field)) {
                    continue;
                }
                wrapper.body_ops.push_back(IRFunction::BodyOp{
                    .kind = IRFunction::BodyOpKind::assign,
                    .name = "in." + field.name,
                    .value = read_intrinsic(field),
                });
            }
        }
    }

    // Call the user entry, forwarding arguments in declared order.
    std::string call = callee + "(";
    for (std::size_t i = 0; i < user.parameters.size(); ++i) {
        if (i) call += ", ";
        if (is_stage_globals_type(stage, user.parameters[i].type)) {
            call += "g";
        } else {
            call += is_stage_builtin_carrier(user.parameters[i].type) ? "bi" : "in";
        }
    }
    call += ")";

    const bool has_output = !output_type.empty() && output_type != "void";
    if (has_output) {
        wrapper.body_ops.push_back(IRFunction::BodyOp{
            .kind = IRFunction::BodyOpKind::assign,
            .name = output_type + " out",
            .value = call,
        });
    } else {
        wrapper.body_ops.push_back(IRFunction::BodyOp{.kind = IRFunction::BodyOpKind::expr, .value = call});
    }

    // Copy-out: write the builtin outputs the shader set back to the gl_* globals.
    for (const auto &builtin : builtins) {
        if (builtin.is_output && body_uses_member(user, builtin.member)) {
            wrapper.body_ops.push_back(IRFunction::BodyOp{
                .kind = IRFunction::BodyOpKind::assign,
                .name = "__rt_write_builtin(\"" + builtin.gl_name + "\")",
                .value = "bi." + builtin.member,
            });
        }
    }

    if (has_output) {
        if (const auto *out_interface = find_interface(interfaces, output_type)) {
            for (const auto &field : out_interface->fields) {
                wrapper.body_ops.push_back(IRFunction::BodyOp{
                    .kind = IRFunction::BodyOpKind::assign,
                    .name = write_target(field),
                    .value = "out." + field.name,
                });
            }
        }
    }

    return wrapper;
}

} // namespace

IRModule lower_to_ir(const SemanticModule &module, DiagnosticEngine *diagnostics) {
    IRModule ir{.source_name = module.source_name};
    ir.structs = module.structs;
    ir.uniforms = module.uniforms;
    ir.stage_interfaces = module.stage_interfaces;

    // Entry functions whose stage we recognized; wrappers are generated after the
    // user functions have all been lowered.
    struct PendingStage {
        std::size_t function_index;
        StageKind stage;
        std::string user_name;
        std::string carrier_type; // builtin carrier parameter type, or ""
        std::string input_type;   // stage input payload (first non-carrier parameter)
        std::string output_type;
    };
    std::vector<PendingStage> pending_stages;

    for (const auto &symbol : module.symbols) {
        if (symbol.kind == DeclKind::function) {
            std::vector<ParameterDecl> parameters;
            parameters.reserve(symbol.parameters.size());
            const auto stage = detect_stage(symbol.name);
            const bool is_stage_entry = stage != StageKind::none;
            const std::string stage_globals_type = is_stage_entry ? globals_type_name(stage) : std::string{};
            bool has_explicit_globals = false;
            for (const auto &parameter : symbol.parameters) {
                if (is_stage_entry && parameter.type == stage_globals_type) {
                    has_explicit_globals = true;
                }
                parameters.push_back(ParameterDecl{.type = parameter.type, .name = parameter.name});
            }
            if (is_stage_entry && !has_explicit_globals) {
                parameters.insert(parameters.begin(), ParameterDecl{.type = stage_globals_type, .name = "g"});
            }
            ir.functions.push_back(IRFunction{
                .name = symbol.name,
                .symbol_name = symbol.name,
                .parameters = std::move(parameters),
                .return_type = symbol.return_type,
            });
            for (const auto &statement : symbol.body_statements) {
                auto op = parse_statement(statement);
                // Lower source-level uniform references to mangled binding names
                // before serialization so source identifiers never leak into the
                // persisted artifact.
                op.name = lower_uniform_references(std::move(op.name), ir.uniforms);
                op.callee = lower_uniform_references(std::move(op.callee), ir.uniforms);
                op.value = lower_uniform_references(std::move(op.value), ir.uniforms);
                for (auto &arg : op.args) {
                    arg = lower_uniform_references(std::move(arg), ir.uniforms);
                }
                ir.functions.back().body_ops.push_back(std::move(op));
            }
            if (is_stage_entry) {
                // The builtin carrier (if any) is a reference parameter of a
                // carrier type; the input payload is the first non-carrier one.
                std::string carrier_type;
                std::string input_type;
                for (const auto &parameter : symbol.parameters) {
                    if (is_stage_builtin_carrier(parameter.type)) {
                        if (carrier_type.empty()) carrier_type = parameter.type;
                    } else if (is_stage_globals_type(stage, parameter.type)) {
                        continue;
                    } else if (input_type.empty()) {
                        input_type = parameter.type;
                    }
                }
                pending_stages.push_back(PendingStage{
                    .function_index = ir.functions.size() - 1,
                    .stage = stage,
                    .user_name = symbol.name,
                    .carrier_type = std::move(carrier_type),
                    .input_type = std::move(input_type),
                    .output_type = symbol.return_type,
                });
            }
        }
    }

    // Generate the compiler-provided backend entry wrappers (the stage runtime).
    // The user entry is demoted to a plain function; the wrapper becomes the
    // stage's real entry point named vert/frag/geom/comp.
    // Plain stage boundaries (vertex attribute inputs, fragment framebuffer
    // outputs, compute builtins) are derived from the payload struct's fields
    // when the source does not declare them. The entry signature drives this:
    // `vert_main(Point p)` means the vertex stage consumes a Point input.
    const auto synth_from_struct = [&](const std::string &type, StageRole role) {
        if (type.empty() || type == "void") {
            return;
        }
        if (find_interface(ir.stage_interfaces, type)) {
            return; // already declared or derived
        }
        for (const auto &struct_decl : ir.structs) {
            if (struct_decl.name != type) {
                continue;
            }
            StageInterface derived{.role = role, .type_name = type};
            u32 location = 0;
            for (const auto &field : struct_decl.fields) {
                derived.fields.push_back(StageIOField{
                    .name = field.name,
                    .location = location++,
                    .has_location = true,
                });
            }
            ir.stage_interfaces.push_back(std::move(derived));
            return;
        }
    };

    // A payload that crosses the rasterizer (a vertex output / fragment input)
    // is NOT derivable: its `varying` declaration defines how each field is
    // interpolated. Require one rather than inventing a plain interface.
    const auto require_varying = [&](const std::string &type, std::string_view context) {
        if (type.empty() || type == "void") {
            return;
        }
        for (const auto &interface : ir.stage_interfaces) {
            if (interface.type_name == type && interface.role == StageRole::varying) {
                return;
            }
        }
        if (diagnostics) {
            diagnostics->report(3100, DiagnosticSeverity::error, {}, module.source_name,
                                "stage payload '" + type + "' is " + std::string(context) +
                                    " and requires a 'varying' declaration to define interpolation");
        }
    };

    for (const auto &pending : pending_stages) {
        switch (pending.stage) {
        case StageKind::vertex:
            synth_from_struct(pending.input_type, StageRole::input);
            break;
        case StageKind::fragment:
            require_varying(pending.input_type, "a fragment input");
            synth_from_struct(pending.output_type, StageRole::output);
            break;
        case StageKind::compute:
            synth_from_struct(pending.input_type, StageRole::input);
            break;
        case StageKind::none:
            break;
        }
    }

    const Mangler mangler;
    for (const auto &pending : pending_stages) {
        auto &user_function = ir.functions[pending.function_index];
        // Reference the user entry by the name the disassembler will show for it.
        // Demoted entries are mangled; entry names contain no '::', so the mangled
        // form needs no further lowering.
        const auto callee = mangler.mangle_rtsl(user_function);
        auto wrapper = make_stage_wrapper(pending.stage, user_function, callee, pending.carrier_type,
                                          pending.input_type, pending.output_type, ir.stage_interfaces);
        ir.functions.push_back(std::move(wrapper));
    }

    return ir;
}

bool verify_ir(const IRModule &) {
    return true;
}

} // namespace rtsl
