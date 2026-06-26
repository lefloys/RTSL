#pragma once

#include "Basic/Diagnostics.h"
#include "Serialization/Artifact.h"

#include <vector>

namespace rtsl {

class Linker {
public:
    explicit Linker(DiagnosticEngine &diagnostics);

    bool add_artifact_bytes(std::span<const u8> bytes);
    bool add_artifact(Artifact artifact);
    [[nodiscard]] Artifact link_program();

private:
    DiagnosticEngine &diagnostics_;
    std::vector<Artifact> inputs_;
};

} // namespace rtsl
