#include "Link/Linker.h"

namespace rtsl {

Linker::Linker(DiagnosticEngine &diagnostics) : diagnostics_(diagnostics) {}

bool Linker::add_artifact_bytes(std::span<const u8> bytes) {
    Artifact artifact;
    if (!read_artifact(bytes, artifact, &diagnostics_)) {
        return false;
    }
    inputs_.push_back(std::move(artifact));
    return true;
}

bool Linker::add_artifact(Artifact artifact) {
    if (artifact.bytes.empty()) {
        diagnostics_.report(6001, DiagnosticSeverity::error, {}, "<link>", "cannot link an empty artifact");
        return false;
    }
    inputs_.push_back(std::move(artifact));
    return true;
}

Artifact Linker::link_program() {
    Artifact program{.kind = ArtifactKind::program};
    if (inputs_.empty()) {
        diagnostics_.report(6002, DiagnosticSeverity::error, {}, "<link>", "no input artifacts provided");
        return program;
    }

    program.bytes = write_linked_program(inputs_);
    program.structs.clear();
    program.uniforms.clear();
    program.functions.clear();
    program.function_debug.clear();
    for (const auto &input : inputs_) {
        for (const auto &struct_decl : input.structs) {
            program.structs.push_back(struct_decl);
        }
        for (const auto &uniform : input.uniforms) {
            program.uniforms.push_back(uniform);
        }
        for (const auto &function : input.functions) {
            program.functions.push_back(function);
        }
        for (const auto &debug : input.function_debug) {
            program.function_debug.push_back(debug);
        }
    }
    return program;
}

} // namespace rtsl
