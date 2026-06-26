#pragma once

#include "IR/IR.h"

#include <string>

namespace rtsl {

class Mangler {
public:
    [[nodiscard]] std::string mangle_rtsl(const IRFunction &function) const;
    [[nodiscard]] std::string mangle_glsl_from_rtsl(std::string_view rtsl_mangled_name) const;
    [[nodiscard]] std::string mangle_for_glsl(const IRFunction &function) const;
};

} // namespace rtsl
