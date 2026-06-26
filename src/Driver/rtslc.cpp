#include "rtsl.h"

#include "Basic/Diagnostics.h"
#include "Serialization/Artifact.h"
#include "Serialization/TextRTIR.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write_file(const std::string &path, rtsl_blob blob) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(blob.data), static_cast<std::streamsize>(blob.size));
    return output.good();
}

void print_diagnostics(rtsl_context ctx) {
    const auto count = rtslCtxGetDiagnosticCount(ctx);
    for (std::size_t i = 0; i < count; ++i) {
        const auto diagnostic = rtslCtxGetDiagnostic(ctx, i);
        std::cerr << diagnostic.source_name << ':' << diagnostic.line << ':' << diagnostic.column
                  << ": " << diagnostic.text << '\n';
    }
}

void print_engine_diagnostics(const rtsl::DiagnosticEngine &diagnostics) {
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.source_name << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": " << diagnostic.message << '\n';
    }
}

void usage() {
    std::cerr << "usage:\n"
              << "  rtslc -c input.rtsl -o output.rtslo\n"
              << "  rtslc --link-program input.rtslo... -o output.rtslp\n"
              << "  rtslc --dump-rtir input.rtslo\n"
              << "  rtslc --assemble-rtir input.rtir -o output.rtslo\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    bool compile = false;
    bool link_program = false;
    bool dump_rtir = false;
    bool assemble_rtir = false;
    std::string output;
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-c") {
            compile = true;
        } else if (arg == "--link-program") {
            link_program = true;
        } else if (arg == "--dump-rtir") {
            dump_rtir = true;
        } else if (arg == "--assemble-rtir") {
            assemble_rtir = true;
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else {
            inputs.push_back(arg);
        }
    }

    const int mode_count = (compile ? 1 : 0) + (link_program ? 1 : 0) + (dump_rtir ? 1 : 0) + (assemble_rtir ? 1 : 0);
    if (inputs.empty() || mode_count != 1 || (!dump_rtir && output.empty())) {
        usage();
        return 1;
    }

    rtsl_context ctx = rtslCreateContext();
    if (!ctx) {
        std::cerr << "failed to create RTSL context\n";
        return 1;
    }

    int exit_code = 0;
    if (compile) {
        const auto source = read_file(inputs.front());
        if (source.empty()) {
            std::cerr << "failed to read " << inputs.front() << '\n';
            rtslDestroyContext(ctx);
            return 1;
        }

        rtsl_module module = rtslCompileSource(ctx, reinterpret_cast<const char *>(source.data()), source.size(), inputs.front().c_str());
        if (!module) {
            print_diagnostics(ctx);
            exit_code = 1;
        } else if (!write_file(output, rtslModuleGetBytecode(module))) {
            std::cerr << "failed to write " << output << '\n';
            exit_code = 1;
        }
        rtslDestroyModule(module);
    } else if (link_program) {
        rtsl_linker linker = rtslCreateLinker(ctx);
        for (const auto &input_path : inputs) {
            const auto bytes = read_file(input_path);
            if (bytes.empty() || !rtslLinkerAddBlob(linker, bytes.data(), bytes.size())) {
                std::cerr << "failed to add input " << input_path << '\n';
                print_diagnostics(ctx);
                exit_code = 1;
                break;
            }
        }
        if (exit_code == 0) {
            rtsl_module program = rtslLinkProgram(linker);
            if (!program) {
                print_diagnostics(ctx);
                exit_code = 1;
            } else if (!write_file(output, rtslModuleGetBytecode(program))) {
                std::cerr << "failed to write " << output << '\n';
                exit_code = 1;
            }
            rtslDestroyModule(program);
        }
        rtslDestroyLinker(linker);
    } else if (dump_rtir) {
        const auto bytes = read_file(inputs.front());
        rtsl::Artifact artifact;
        rtsl::DiagnosticEngine diagnostics;
        if (bytes.empty() || !rtsl::read_artifact(bytes, artifact, &diagnostics)) {
            std::cerr << "failed to read artifact " << inputs.front() << '\n';
            print_engine_diagnostics(diagnostics);
            exit_code = 1;
        } else {
            std::cout << rtsl::disassemble_artifact(artifact);
        }
    } else if (assemble_rtir) {
        const auto bytes = read_file(inputs.front());
        rtsl::Artifact artifact;
        rtsl::DiagnosticEngine diagnostics;
        const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        if (bytes.empty() || !rtsl::assemble_text_rtir(text, artifact, &diagnostics)) {
            std::cerr << "failed to assemble RTIR " << inputs.front() << '\n';
            print_engine_diagnostics(diagnostics);
            exit_code = 1;
        } else if (!write_file(output, rtsl_blob{artifact.bytes.data(), artifact.bytes.size()})) {
            std::cerr << "failed to write " << output << '\n';
            exit_code = 1;
        }
    }

    rtslDestroyContext(ctx);
    return exit_code;
}
