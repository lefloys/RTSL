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
    for (const auto &input : inputs_) {
        for (const auto &struct_decl : input.structs) {
            program.structs.push_back(struct_decl);
        }
        for (const auto &uniform : input.uniforms) {
            program.uniforms.push_back(uniform);
        }
        for (const auto &interface : input.stage_interfaces) {
            program.stage_interfaces.push_back(interface);
        }
        for (const auto &function : input.functions) {
            program.functions.push_back(function);
        }
    }

    validate_program_stages(program);
    return program;
}

void Linker::validate_program_stages(const Artifact &program) {
    bool has_vertex = false;
    bool has_fragment = false;
    bool has_compute = false;
    for (const auto &function : program.functions) {
        if (function.stage == StageKind::none) {
            continue;
        }
        switch (function.stage) {
        case StageKind::vertex: has_vertex = true; break;
        case StageKind::fragment: has_fragment = true; break;
        case StageKind::compute: has_compute = true; break;
        case StageKind::none: break;
        }
    }

    const auto error = [&](std::string message) {
        diagnostics_.report(6003, DiagnosticSeverity::error, {}, "<link>", std::move(message));
    };

    if (has_compute) {
        // A compute program is standalone; it must not be mixed with graphics stages.
        if (has_vertex || has_fragment) {
            error("compute stage (comp) cannot be combined with graphics stages in one program");
        }
        return;
    }

    // A program that declares any graphics stage is a graphics program and must
    // provide at least a vertex and a fragment stage.
    if (has_vertex || has_fragment) {
        if (!has_vertex) {
            error("graphics program is missing a vertex stage (vert)");
        }
        if (!has_fragment) {
            error("graphics program is missing a fragment stage (frag)");
        }
    }
}

} // namespace rtsl
