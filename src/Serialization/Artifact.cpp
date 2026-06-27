#include "Serialization/Artifact.h"

#include <array>
#include <cstring>
#include <optional>

namespace rtsl {

namespace {

constexpr u32 Magic = 0x4c535452; // RTSL, little-endian bytes.
constexpr u16 VersionMajor = 1;
constexpr u16 VersionMinor = 0;
constexpr u32 HeaderSize = 48;
constexpr u32 SectionEntrySize = 32;

enum class SectionKind : u32 {
    string_table = 1,
    type_table = 2,
    symbol_table = 3,
    function_table = 4,
    debug_table = 5,
    struct_table = 7,
    uniform_table = 9,
    stage_interface_table = 10,
};

struct Section {
    SectionKind kind;
    std::vector<u8> bytes;
};

void append_u8(std::vector<u8> &out, u8 value) {
    out.push_back(value);
}

void append_u16(std::vector<u8> &out, u16 value) {
    out.push_back(static_cast<u8>(value & 0xff));
    out.push_back(static_cast<u8>((value >> 8) & 0xff));
}

void append_u32(std::vector<u8> &out, u32 value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<u8>((value >> (i * 8)) & 0xff));
    }
}

void append_u64(std::vector<u8> &out, u64 value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<u8>((value >> (i * 8)) & 0xff));
    }
}

u16 read_u16(std::span<const u8> data, std::size_t offset) {
    return static_cast<u16>(data[offset] | (data[offset + 1] << 8));
}

u32 read_u32(std::span<const u8> data, std::size_t offset) {
    return static_cast<u32>(data[offset]) |
           (static_cast<u32>(data[offset + 1]) << 8) |
           (static_cast<u32>(data[offset + 2]) << 16) |
           (static_cast<u32>(data[offset + 3]) << 24);
}

u64 read_u64(std::span<const u8> data, std::size_t offset) {
    u64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<u64>(data[offset + i]) << (i * 8);
    }
    return value;
}

void append_string(std::vector<u8> &out, std::string_view text) {
    append_u32(out, static_cast<u32>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

Section make_string_table(const IRModule &module) {
    Section section{.kind = SectionKind::string_table};
    append_u32(section.bytes, static_cast<u32>(1 + module.functions.size() * 2));
    append_string(section.bytes, module.source_name);
    for (const auto &function : module.functions) {
        append_string(section.bytes, function.name);
        append_string(section.bytes, function.symbol_name.empty() ? function.name : function.symbol_name);
    }
    return section;
}

Section make_empty_section(SectionKind kind) {
    Section section{.kind = kind};
    append_u32(section.bytes, 0);
    return section;
}

Section make_function_table(const IRModule &module) {
    Section section{.kind = SectionKind::function_table};
    append_u32(section.bytes, static_cast<u32>(module.functions.size()));
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        append_u32(section.bytes, static_cast<u32>(1 + i * 2));
        append_u32(section.bytes, static_cast<u32>(2 + i * 2));
        append_u8(section.bytes, static_cast<u8>(module.functions[i].stage));
        append_u8(section.bytes, module.functions[i].generated ? 1 : 0);
        append_string(section.bytes, module.functions[i].return_type.empty() ? "void" : module.functions[i].return_type);
        append_u32(section.bytes, static_cast<u32>(module.functions[i].parameters.size()));
        for (const auto &parameter : module.functions[i].parameters) {
            append_string(section.bytes, parameter.type);
            append_string(section.bytes, parameter.name);
        }
        // Function bodies must not be serialized as source text. This placeholder
        // keeps the section version stable until structured statement IR lands.
        append_u32(section.bytes, static_cast<u32>(module.functions[i].body_ops.size()));
        for (const auto &op : module.functions[i].body_ops) {
            append_u8(section.bytes, static_cast<u8>(op.kind));
            append_string(section.bytes, op.type);
            append_string(section.bytes, op.name);
            append_string(section.bytes, op.op);
            append_string(section.bytes, op.callee);
            append_u32(section.bytes, static_cast<u32>(op.args.size()));
            for (const auto &arg : op.args) {
                append_string(section.bytes, arg);
            }
            append_string(section.bytes, op.value);
        }
    }
    return section;
}

Section make_struct_table(const IRModule &module) {
    Section section{.kind = SectionKind::struct_table};
    append_u32(section.bytes, static_cast<u32>(module.structs.size()));
    for (const auto &struct_decl : module.structs) {
        append_string(section.bytes, struct_decl.name);
        append_u32(section.bytes, static_cast<u32>(struct_decl.fields.size()));
        for (const auto &field : struct_decl.fields) {
            append_string(section.bytes, field.type);
            append_string(section.bytes, field.name);
        }
    }
    return section;
}

Section make_uniform_table(const IRModule &module) {
    Section section{.kind = SectionKind::uniform_table};
    append_u32(section.bytes, static_cast<u32>(module.uniforms.size()));
    for (const auto &uniform : module.uniforms) {
        append_string(section.bytes, uniform.scope_name);
        append_string(section.bytes, uniform.name);
        append_string(section.bytes, uniform.type);
        append_string(section.bytes, uniform.access);
        append_u32(section.bytes, uniform.set);
        append_u32(section.bytes, uniform.binding);
        append_u32(section.bytes, static_cast<u32>(uniform.inline_fields.size()));
        for (const auto &field : uniform.inline_fields) {
            append_string(section.bytes, field.type);
            append_string(section.bytes, field.name);
        }
    }
    return section;
}

Section make_stage_interface_table(const IRModule &module) {
    Section section{.kind = SectionKind::stage_interface_table};
    append_u32(section.bytes, static_cast<u32>(module.stage_interfaces.size()));
    for (const auto &interface : module.stage_interfaces) {
        append_u8(section.bytes, static_cast<u8>(interface.role));
        append_string(section.bytes, interface.type_name);
        append_u32(section.bytes, static_cast<u32>(interface.fields.size()));
        for (const auto &field : interface.fields) {
            append_string(section.bytes, field.name);
            append_string(section.bytes, field.interpolation);
            append_string(section.bytes, field.builtin);
            append_u8(section.bytes, field.has_location ? 1 : 0);
            append_u32(section.bytes, field.location);
        }
    }
    return section;
}

std::vector<u8> write_container(ArtifactKind kind, std::vector<Section> sections) {
    const u32 section_count = static_cast<u32>(sections.size());
    const u64 section_table_offset = HeaderSize;
    u64 data_offset = HeaderSize + static_cast<u64>(SectionEntrySize) * section_count;

    std::vector<u64> offsets;
    offsets.reserve(sections.size());
    for (const auto &section : sections) {
        offsets.push_back(data_offset);
        data_offset += section.bytes.size();
    }

    std::vector<u8> out;
    append_u32(out, Magic);
    append_u16(out, VersionMajor);
    append_u16(out, VersionMinor);
    append_u16(out, static_cast<u16>(kind));
    append_u8(out, 1);
    append_u8(out, 0);
    append_u32(out, 0);
    append_u32(out, HeaderSize);
    append_u32(out, section_count);
    append_u64(out, section_table_offset);
    append_u64(out, data_offset);
    out.resize(HeaderSize, 0);

    for (std::size_t i = 0; i < sections.size(); ++i) {
        append_u32(out, static_cast<u32>(sections[i].kind));
        append_u32(out, 0);
        append_u64(out, offsets[i]);
        append_u64(out, static_cast<u64>(sections[i].bytes.size()));
        append_u32(out, 1);
        append_u32(out, 0);
    }

    for (const auto &section : sections) {
        out.insert(out.end(), section.bytes.begin(), section.bytes.end());
    }

    return out;
}

void report_read_error(DiagnosticEngine *diagnostics, std::string message) {
    if (diagnostics) {
        diagnostics->report(5001, DiagnosticSeverity::error, {}, "<artifact>", std::move(message));
    }
}

} // namespace

std::vector<u8> write_artifact(ArtifactKind kind, const IRModule &module) {
    std::vector<Section> sections;
    sections.push_back(make_string_table(module));
    sections.push_back(make_empty_section(SectionKind::type_table));
    sections.push_back(make_empty_section(SectionKind::symbol_table));
    sections.push_back(make_struct_table(module));
    sections.push_back(make_uniform_table(module));
    sections.push_back(make_stage_interface_table(module));
    sections.push_back(make_function_table(module));
    sections.push_back(make_empty_section(SectionKind::debug_table));
    return write_container(kind, std::move(sections));
}

std::vector<u8> write_debug_artifact(const IRModule &module) {
    return write_container(ArtifactKind::object, {});
}

std::vector<u8> write_linked_program(std::span<const Artifact> inputs) {
    IRModule module{.source_name = "<linked>"};
    for (const auto &input : inputs) {
        for (const auto &struct_decl : input.structs) {
            module.structs.push_back(struct_decl);
        }
        for (const auto &uniform : input.uniforms) {
            module.uniforms.push_back(uniform);
        }
        for (const auto &interface : input.stage_interfaces) {
            module.stage_interfaces.push_back(interface);
        }
        for (const auto &function : input.functions) {
            module.functions.push_back(function);
        }
    }
    return write_artifact(ArtifactKind::program, module);
}

bool read_artifact(std::span<const u8> data, Artifact &artifact, DiagnosticEngine *diagnostics) {
    if (data.size() < HeaderSize) {
        report_read_error(diagnostics, "artifact is smaller than the header");
        return false;
    }
    if (read_u32(data, 0) != Magic) {
        report_read_error(diagnostics, "invalid RTSL artifact magic");
        return false;
    }
    if (read_u16(data, 4) != VersionMajor) {
        report_read_error(diagnostics, "unsupported RTSL artifact version");
        return false;
    }
    const auto kind = static_cast<ArtifactKind>(read_u16(data, 8));
    const auto endian = data[10];
    const auto header_size = read_u32(data, 16);
    const auto section_count = read_u32(data, 20);
    const auto section_table_offset = read_u64(data, 24);
    const auto file_size = read_u64(data, 32);

    if (endian != 1) {
        report_read_error(diagnostics, "RTSL artifacts must be little-endian");
        return false;
    }
    if (header_size != HeaderSize || file_size != data.size()) {
        report_read_error(diagnostics, "invalid RTSL artifact header bounds");
        return false;
    }
    if (section_table_offset + static_cast<u64>(section_count) * SectionEntrySize > data.size()) {
        report_read_error(diagnostics, "section table is out of bounds");
        return false;
    }

    artifact = Artifact{.kind = kind, .bytes = std::vector<u8>(data.begin(), data.end())};

    bool saw_string_table = false;
    std::vector<u8> struct_section;
    std::vector<u8> uniform_section;
    std::vector<u8> function_section;
    std::vector<u8> stage_interface_section;
    for (u32 i = 0; i < section_count; ++i) {
        const auto entry = static_cast<std::size_t>(section_table_offset + i * SectionEntrySize);
        const auto section_kind = static_cast<SectionKind>(read_u32(data, entry));
        const auto offset = read_u64(data, entry + 8);
        const auto size = read_u64(data, entry + 16);
        if (offset + size > data.size()) {
            report_read_error(diagnostics, "section payload is out of bounds");
            return false;
        }
        if (section_kind == SectionKind::function_table) {
            auto section = data.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
            function_section.assign(section.begin(), section.end());
        }
        if (section_kind == SectionKind::struct_table) {
            auto section = data.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
            struct_section.assign(section.begin(), section.end());
        }
        if (section_kind == SectionKind::uniform_table) {
            auto section = data.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
            uniform_section.assign(section.begin(), section.end());
        }
        if (section_kind == SectionKind::stage_interface_table) {
            auto section = data.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
            stage_interface_section.assign(section.begin(), section.end());
        }
        if (section_kind != SectionKind::string_table) {
            continue;
        }

        saw_string_table = true;
        auto section = data.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
        if (section.size() < 4) {
            report_read_error(diagnostics, "string table is truncated");
            return false;
        }
        const auto count = read_u32(section, 0);
        std::size_t cursor = 4;
        for (u32 string_index = 0; string_index < count; ++string_index) {
            if (cursor + 4 > section.size()) {
                report_read_error(diagnostics, "string table entry is truncated");
                return false;
            }
            const auto length = read_u32(section, cursor);
            cursor += 4;
            if (cursor + length > section.size()) {
                report_read_error(diagnostics, "string payload is truncated");
                return false;
            }
            artifact.strings.emplace_back(reinterpret_cast<const char *>(section.data() + cursor), length);
            cursor += length;
        }
    }

    if (!saw_string_table) {
        report_read_error(diagnostics, "artifact is missing the string table");
        return false;
    }
    if (!struct_section.empty()) {
        std::span<const u8> section(struct_section);
        if (section.size() < 4) {
            report_read_error(diagnostics, "struct table is truncated");
            return false;
        }
        const auto count = read_u32(section, 0);
        std::size_t cursor = 4;
        for (u32 struct_index = 0; struct_index < count; ++struct_index) {
            if (cursor + 8 > section.size()) {
                report_read_error(diagnostics, "struct table entry is truncated");
                return false;
            }
            const auto name_length = read_u32(section, cursor);
            cursor += 4;
            if (cursor + name_length + 4 > section.size()) {
                report_read_error(diagnostics, "struct name is truncated");
                return false;
            }
            StructDecl struct_decl;
            struct_decl.name = std::string(reinterpret_cast<const char *>(section.data() + cursor), name_length);
            cursor += name_length;
            const auto field_count = read_u32(section, cursor);
            cursor += 4;
            for (u32 field_index = 0; field_index < field_count; ++field_index) {
                if (cursor + 8 > section.size()) {
                    report_read_error(diagnostics, "struct field is truncated");
                    return false;
                }
                const auto type_length = read_u32(section, cursor);
                cursor += 4;
                if (cursor + type_length + 4 > section.size()) {
                    report_read_error(diagnostics, "struct field type is truncated");
                    return false;
                }
                std::string type(reinterpret_cast<const char *>(section.data() + cursor), type_length);
                cursor += type_length;
                const auto field_name_length = read_u32(section, cursor);
                cursor += 4;
                if (cursor + field_name_length > section.size()) {
                    report_read_error(diagnostics, "struct field name is truncated");
                    return false;
                }
                std::string name(reinterpret_cast<const char *>(section.data() + cursor), field_name_length);
                cursor += field_name_length;
                struct_decl.fields.push_back(StructField{.type = std::move(type), .name = std::move(name)});
            }
            artifact.structs.push_back(std::move(struct_decl));
        }
    }
    if (!function_section.empty()) {
        std::span<const u8> section(function_section);
        if (section.size() < 4) {
            report_read_error(diagnostics, "function table is truncated");
            return false;
        }
        const auto count = read_u32(section, 0);
        std::size_t cursor = 4;
        for (u32 function_index = 0; function_index < count; ++function_index) {
            if (cursor + 14 > section.size()) {
                report_read_error(diagnostics, "function table entry is truncated");
                return false;
            }
            const auto display_string_id = read_u32(section, cursor);
            cursor += 4;
            const auto symbol_string_id = read_u32(section, cursor);
            cursor += 4;
            const auto stage = static_cast<StageKind>(section[cursor++]);
            const bool generated = section[cursor++] != 0;
            if (display_string_id >= artifact.strings.size() || symbol_string_id >= artifact.strings.size()) {
                report_read_error(diagnostics, "function table references an invalid string");
                return false;
            }
            const auto return_type_length = read_u32(section, cursor);
            cursor += 4;
            if (cursor + return_type_length + 4 > section.size()) {
                report_read_error(diagnostics, "function return type is truncated");
                return false;
            }
            std::string return_type(reinterpret_cast<const char *>(section.data() + cursor), return_type_length);
            cursor += return_type_length;
            const auto parameter_count = read_u32(section, cursor);
            cursor += 4;
            std::vector<ParameterDecl> parameters;
            for (u32 parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
                if (cursor + 4 > section.size()) {
                    report_read_error(diagnostics, "function parameter is truncated");
                    return false;
                }
                const auto parameter_length = read_u32(section, cursor);
                cursor += 4;
                if (cursor + parameter_length > section.size()) {
                    report_read_error(diagnostics, "function parameter payload is truncated");
                    return false;
                }
                std::string parameter_type(reinterpret_cast<const char *>(section.data() + cursor), parameter_length);
                cursor += parameter_length;
                if (cursor + 4 > section.size()) {
                    report_read_error(diagnostics, "function parameter name is truncated");
                    return false;
                }
                const auto parameter_name_length = read_u32(section, cursor);
                cursor += 4;
                if (cursor + parameter_name_length > section.size()) {
                    report_read_error(diagnostics, "function parameter name payload is truncated");
                    return false;
                }
                std::string parameter_name(reinterpret_cast<const char *>(section.data() + cursor), parameter_name_length);
                cursor += parameter_name_length;
                parameters.push_back(ParameterDecl{.type = std::move(parameter_type), .name = std::move(parameter_name)});
            }
            if (cursor + 4 > section.size()) {
                report_read_error(diagnostics, "function body table is truncated");
                return false;
            }
            const auto statement_count = read_u32(section, cursor);
            cursor += 4;
            std::vector<IRFunction::BodyOp> body_ops;
            for (u32 statement_index = 0; statement_index < statement_count; ++statement_index) {
                if (cursor + 1 > section.size()) {
                    report_read_error(diagnostics, "function body op is truncated");
                    return false;
                }
                IRFunction::BodyOp op;
                op.kind = static_cast<IRFunction::BodyOpKind>(section[cursor++]);
                auto read_string = [&]() -> std::optional<std::string> {
                    if (cursor + 4 > section.size()) return std::nullopt;
                    const auto length = read_u32(section, cursor);
                    cursor += 4;
                    if (cursor + length > section.size()) return std::nullopt;
                    std::string value(reinterpret_cast<const char *>(section.data() + cursor), length);
                    cursor += length;
                    return value;
                };
                auto type = read_string(); auto name = read_string(); auto oper = read_string(); auto callee = read_string();
                if (!type || !name || !oper || !callee || cursor + 4 > section.size()) { report_read_error(diagnostics, "function body op is truncated"); return false; }
                op.type = std::move(*type); op.name = std::move(*name); op.op = std::move(*oper); op.callee = std::move(*callee);
                const auto arg_count = read_u32(section, cursor); cursor += 4;
                for (u32 arg_index = 0; arg_index < arg_count; ++arg_index) {
                    auto arg = read_string();
                    if (!arg) { report_read_error(diagnostics, "function body op arg is truncated"); return false; }
                    op.args.push_back(std::move(*arg));
                }
                auto value = read_string();
                if (!value) { report_read_error(diagnostics, "function body op value is truncated"); return false; }
                op.value = std::move(*value);
                body_ops.push_back(std::move(op));
            }
            artifact.functions.push_back(IRFunction{
                .name = artifact.strings[display_string_id],
                .symbol_name = artifact.strings[symbol_string_id],
                .parameters = std::move(parameters),
                .return_type = std::move(return_type),
                .body_ops = std::move(body_ops),
                .stage = stage,
                .generated = generated,
            });
        }
    }
    if (!uniform_section.empty()) {
        std::span<const u8> section(uniform_section);
        if (section.size() < 4) {
            report_read_error(diagnostics, "uniform table is truncated");
            return false;
        }
        const auto count = read_u32(section, 0);
        std::size_t cursor = 4;
        auto read_string = [&]() -> std::optional<std::string> {
            if (cursor + 4 > section.size()) {
                return std::nullopt;
            }
            const auto length = read_u32(section, cursor);
            cursor += 4;
            if (cursor + length > section.size()) {
                return std::nullopt;
            }
            std::string value(reinterpret_cast<const char *>(section.data() + cursor), length);
            cursor += length;
            return value;
        };
        for (u32 uniform_index = 0; uniform_index < count; ++uniform_index) {
            auto scope_name = read_string();
            auto name = read_string();
            auto type = read_string();
            auto access = read_string();
            if (!scope_name || !name || !type || !access || cursor + 12 > section.size()) {
                report_read_error(diagnostics, "uniform table entry is truncated");
                return false;
            }
            UniformBinding uniform{
                .scope_name = std::move(*scope_name),
                .name = std::move(*name),
                .type = std::move(*type),
                .access = std::move(*access),
                .set = read_u32(section, cursor),
                .binding = read_u32(section, cursor + 4),
            };
            cursor += 8;
            const auto field_count = read_u32(section, cursor);
            cursor += 4;
            for (u32 field_index = 0; field_index < field_count; ++field_index) {
                auto field_type = read_string();
                auto field_name = read_string();
                if (!field_type || !field_name) {
                    report_read_error(diagnostics, "uniform inline field is truncated");
                    return false;
                }
                uniform.inline_fields.push_back(StructField{.type = std::move(*field_type), .name = std::move(*field_name)});
            }
            artifact.uniforms.push_back(std::move(uniform));
        }
    }
    if (!stage_interface_section.empty()) {
        std::span<const u8> section(stage_interface_section);
        if (section.size() < 4) {
            report_read_error(diagnostics, "stage interface table is truncated");
            return false;
        }
        const auto count = read_u32(section, 0);
        std::size_t cursor = 4;
        auto read_string = [&]() -> std::optional<std::string> {
            if (cursor + 4 > section.size()) {
                return std::nullopt;
            }
            const auto length = read_u32(section, cursor);
            cursor += 4;
            if (cursor + length > section.size()) {
                return std::nullopt;
            }
            std::string value(reinterpret_cast<const char *>(section.data() + cursor), length);
            cursor += length;
            return value;
        };
        for (u32 interface_index = 0; interface_index < count; ++interface_index) {
            if (cursor + 1 > section.size()) {
                report_read_error(diagnostics, "stage interface entry is truncated");
                return false;
            }
            StageInterface interface;
            interface.role = static_cast<StageRole>(section[cursor++]);
            auto type_name = read_string();
            if (!type_name || cursor + 4 > section.size()) {
                report_read_error(diagnostics, "stage interface type is truncated");
                return false;
            }
            interface.type_name = std::move(*type_name);
            const auto field_count = read_u32(section, cursor);
            cursor += 4;
            for (u32 field_index = 0; field_index < field_count; ++field_index) {
                auto name = read_string();
                auto interpolation = read_string();
                auto builtin = read_string();
                if (!name || !interpolation || !builtin || cursor + 5 > section.size()) {
                    report_read_error(diagnostics, "stage interface field is truncated");
                    return false;
                }
                StageIOField field;
                field.name = std::move(*name);
                field.interpolation = std::move(*interpolation);
                field.builtin = std::move(*builtin);
                field.has_location = section[cursor++] != 0;
                field.location = read_u32(section, cursor);
                cursor += 4;
                interface.fields.push_back(std::move(field));
            }
            artifact.stage_interfaces.push_back(std::move(interface));
        }
    }
    return true;
}

const char *artifact_extension(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::object: return ".rtslo";
    case ArtifactKind::module: return ".rtslm";
    case ArtifactKind::library: return ".rtsll";
    case ArtifactKind::program: return ".rtslp";
    }
    return ".rtslbin";
}

const char *debug_artifact_extension() {
    return ".rtsld";
}

} // namespace rtsl
