#include "Basic/Diagnostics.h"

namespace rtsl {

void DiagnosticEngine::clear() {
    diagnostics_.clear();
}

void DiagnosticEngine::report(int code, DiagnosticSeverity severity, SourceLocation location,
                              std::string source_name, std::string message) {
    diagnostics_.push_back(Diagnostic{
        .code = code,
        .severity = severity,
        .source_name = std::move(source_name),
        .location = location,
        .message = std::move(message),
    });
}

bool DiagnosticEngine::has_error() const {
    for (const auto &diagnostic : diagnostics_) {
        if (diagnostic.severity == DiagnosticSeverity::error ||
            diagnostic.severity == DiagnosticSeverity::fatal) {
            return true;
        }
    }
    return false;
}

} // namespace rtsl
