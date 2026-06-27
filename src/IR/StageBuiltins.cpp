#include "IR/StageBuiltins.h"

namespace rtsl {

std::vector<StageBuiltin> stage_builtins(std::string_view carrier_type) {
    if (carrier_type == "RtVertex") {
        return {
            StageBuiltin{.member = "position", .type = "vec4", .gl_name = "gl_Position", .is_output = true},
            StageBuiltin{.member = "point_size", .type = "float", .gl_name = "gl_PointSize", .is_output = true},
            StageBuiltin{.member = "vertex_index", .type = "i32", .gl_name = "gl_VertexIndex", .is_output = false},
            StageBuiltin{.member = "instance_index", .type = "i32", .gl_name = "gl_InstanceIndex", .is_output = false},
        };
    }
    if (carrier_type == "RtFragment") {
        return {
            StageBuiltin{.member = "frag_coord", .type = "vec4", .gl_name = "gl_FragCoord", .is_output = false},
            StageBuiltin{.member = "front_facing", .type = "bool", .gl_name = "gl_FrontFacing", .is_output = false},
            StageBuiltin{.member = "frag_depth", .type = "f32", .gl_name = "gl_FragDepth", .is_output = true},
        };
    }
    if (carrier_type == "RtCompute") {
        return {
            StageBuiltin{.member = "global_id", .type = "uvec3", .gl_name = "gl_GlobalInvocationID", .is_output = false},
            StageBuiltin{.member = "local_id", .type = "uvec3", .gl_name = "gl_LocalInvocationID", .is_output = false},
            StageBuiltin{.member = "group_id", .type = "uvec3", .gl_name = "gl_WorkGroupID", .is_output = false},
        };
    }
    return {};
}

bool is_stage_builtin_carrier(std::string_view type) {
    return !stage_builtins(type).empty();
}

} // namespace rtsl
