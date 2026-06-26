#include "Sema/Sema.h"

#include <unordered_map>

namespace rtsl {

Sema::Sema(SourceManager &sources, DiagnosticEngine &diagnostics)
    : sources_(sources), diagnostics_(diagnostics) {}

SemanticModule Sema::analyze(const TranslationUnit &unit) {
    SemanticModule module{.source_name = std::string(sources_.name(unit.file_id))};
    module.structs = unit.structs;
    module.uniforms = unit.uniforms;
    std::unordered_map<std::string, u32> named_sets;
    u32 next_set = 0;
    std::unordered_map<u32, u32> binding_counts;
    for (auto &uniform : module.uniforms) {
        if (uniform.scope_name.empty()) {
            uniform.set = next_set++;
        } else {
            auto [it, inserted] = named_sets.emplace(uniform.scope_name, next_set);
            if (inserted) {
                uniform.set = next_set++;
            } else {
                uniform.set = it->second;
            }
        }
        uniform.binding = binding_counts[uniform.set]++;
    }

    for (const auto &decl : unit.declarations) {
        if (decl.kind == DeclKind::namespace_decl && decl.name == "rt") {
            diagnostics_.report(3001, DiagnosticSeverity::error, decl.span.begin,
                                module.source_name, "namespace 'rt' is reserved");
            continue;
        }

        module.symbols.push_back(SemanticSymbol{
            .kind = decl.kind,
            .name = decl.name,
            .parameters = decl.parameters,
            .return_type = decl.return_type,
            .body_statements = decl.body_statements,
            .exported = decl.exported,
            .entry = decl.entry,
        });
    }

    return module;
}

} // namespace rtsl
