#include "rtsl.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t makeMagic(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
        (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8u) |
        (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16u) |
        (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

constexpr uint32_t RtsoMagic = makeMagic('R', 'T', 'S', 'O');
constexpr uint32_t RtspMagic = makeMagic('R', 'T', 'S', 'P');
constexpr uint16_t FormatVersion = 1;

struct BytecodeHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t kind;
    uint32_t header_size;
    uint32_t payload_size;
};

void appendBytes(std::vector<uint8_t>& bytes, const void* data, size_t size) {
    const auto* first = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    appendBytes(bytes, &value, sizeof(value));
}

void appendString(std::vector<uint8_t>& bytes, std::string_view value) {
    appendU32(bytes, static_cast<uint32_t>(value.size()));
    appendBytes(bytes, value.data(), value.size());
}

std::vector<uint8_t> buildArtifact(
    rtsl_output_kind kind,
    uint32_t magic,
    std::string_view source_name,
    std::string_view payload) {
    std::vector<uint8_t> payload_bytes;
    appendString(payload_bytes, source_name);
    appendString(payload_bytes, payload);

    const BytecodeHeader header{
        magic,
        FormatVersion,
        static_cast<uint16_t>(kind),
        static_cast<uint32_t>(sizeof(BytecodeHeader)),
        static_cast<uint32_t>(payload_bytes.size()),
    };

    std::vector<uint8_t> bytes;
    bytes.reserve(sizeof(BytecodeHeader) + payload_bytes.size());
    appendBytes(bytes, &header, sizeof(header));
    bytes.insert(bytes.end(), payload_bytes.begin(), payload_bytes.end());
    return bytes;
}

void setResult(rtsl_context_t& ctx, int code, std::string text);

}

struct rtsl_context_t {
    rtsl_result result{0, "ok"};
    std::string result_text{"ok"};
};

struct rtsl_module_t {
    rtsl_output_kind kind{RTSL_OUTPUT_OBJECT};
    std::vector<uint8_t> bytecode;
};

struct rtsl_linker_t {
    rtsl_context context{};
    std::vector<rtsl_module> modules;
};

namespace {

void setResult(rtsl_context_t& ctx, int code, std::string text) {
    ctx.result_text = std::move(text);
    ctx.result = rtsl_result{code, ctx.result_text.c_str()};
}

}

extern "C" {

rtsl_context rtslCreateContext(void) {
    auto* ctx = new (std::nothrow) rtsl_context_t();
    return ctx;
}

rtsl_result rtslCtxGetResult(rtsl_context ctx) {
    if (!ctx) {
        return rtsl_result{1, "null RTSL context"};
    }

    return ctx->result;
}

void rtslDestroyContext(rtsl_context ctx) {
    delete ctx;
}

rtsl_module rtslCompileSource(
    rtsl_context ctx,
    const char* source,
    size_t source_size,
    const char* source_name) {
    if (!ctx) {
        return nullptr;
    }

    if (!source && source_size != 0) {
        setResult(*ctx, 1, "source pointer is null");
        return nullptr;
    }

    std::string_view source_view{source ? source : "", source_size};
    std::string_view name_view{source_name ? source_name : "<memory>"};

    auto module = std::make_unique<rtsl_module_t>();
    module->kind = RTSL_OUTPUT_OBJECT;
    module->bytecode = buildArtifact(RTSL_OUTPUT_OBJECT, RtsoMagic, name_view, source_view);

    setResult(*ctx, 0, "ok");
    return module.release();
}

rtsl_blob rtslModuleGetBytecode(rtsl_module module) {
    if (!module || module->bytecode.empty()) {
        return rtsl_blob{nullptr, 0};
    }

    return rtsl_blob{module->bytecode.data(), module->bytecode.size()};
}

rtsl_output_kind rtslModuleGetKind(rtsl_module module) {
    if (!module) {
        return RTSL_OUTPUT_OBJECT;
    }

    return module->kind;
}

void rtslDestroyModule(rtsl_module module) {
    delete module;
}

rtsl_linker rtslCreateLinker(rtsl_context ctx) {
    if (!ctx) {
        return nullptr;
    }

    auto* linker = new (std::nothrow) rtsl_linker_t();
    if (!linker) {
        setResult(*ctx, 1, "failed to allocate RTSL linker");
        return nullptr;
    }

    linker->context = ctx;
    setResult(*ctx, 0, "ok");
    return linker;
}

int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module) {
    if (!linker || !linker->context) {
        return 0;
    }

    if (!module) {
        setResult(*linker->context, 1, "cannot link null module");
        return 0;
    }

    linker->modules.push_back(module);
    setResult(*linker->context, 0, "ok");
    return 1;
}

rtsl_module rtslLinkProgram(rtsl_linker linker) {
    if (!linker || !linker->context) {
        return nullptr;
    }

    if (linker->modules.empty()) {
        setResult(*linker->context, 1, "cannot link program without modules");
        return nullptr;
    }

    std::vector<uint8_t> payload;
    appendU32(payload, static_cast<uint32_t>(linker->modules.size()));
    for (rtsl_module module : linker->modules) {
        const rtsl_blob bytecode = rtslModuleGetBytecode(module);
        appendU32(payload, static_cast<uint32_t>(bytecode.size));
        appendBytes(payload, bytecode.data, bytecode.size);
    }

    auto program = std::make_unique<rtsl_module_t>();
    program->kind = RTSL_OUTPUT_PROGRAM;
    program->bytecode = buildArtifact(
        RTSL_OUTPUT_PROGRAM,
        RtspMagic,
        "<linked-program>",
        std::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()});

    setResult(*linker->context, 0, "ok");
    return program.release();
}

void rtslDestroyLinker(rtsl_linker linker) {
    delete linker;
}

}
