#pragma once

#include "Basic/Diagnostics.h"
#include "Basic/SourceManager.h"
#include "Serialization/Artifact.h"

#include <string>

namespace rtsl {

struct CompilerInvocation {
    std::string source_name = "<memory>";
};

class CompilerInstance {
public:
    [[nodiscard]] Artifact compile_source(std::string source, CompilerInvocation invocation = {});

    [[nodiscard]] DiagnosticEngine &diagnostics() { return diagnostics_; }
    [[nodiscard]] SourceManager &sources() { return sources_; }

private:
    SourceManager sources_;
    DiagnosticEngine diagnostics_;
};

} // namespace rtsl
