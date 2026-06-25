#include "rtsl.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cerr << "usage: rtslc <input.rtsl> [-o <output.rtso>] [--link]\n";
}

std::string defaultOutputPath(const std::string& input, bool link) {
    const size_t slash = input.find_last_of("/\\");
    const size_t dot = input.find_last_of('.');
    const bool has_extension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string base = has_extension ? input.substr(0, dot) : input;
    return base + (link ? ".rtsp" : ".rtso");
}

bool readFile(const std::string& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return false;
    }

    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!out.empty()) {
        file.read(out.data(), static_cast<std::streamsize>(out.size()));
    }

    return file.good() || file.eof();
}

bool writeFile(const std::string& path, rtsl_blob blob) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    if (blob.size != 0) {
        file.write(reinterpret_cast<const char*>(blob.data), static_cast<std::streamsize>(blob.size));
    }

    return file.good();
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    std::string input;
    std::string output;
    bool link = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            output = argv[++i];
        } else if (arg == "--link") {
            link = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (input.empty()) {
            input = arg;
        } else {
            printUsage();
            return 2;
        }
    }

    if (input.empty()) {
        printUsage();
        return 2;
    }

    if (output.empty()) {
        output = defaultOutputPath(input, link);
    }

    std::vector<char> source;
    if (!readFile(input, source)) {
        std::cerr << "rtslc: failed to read input: " << input << "\n";
        return 1;
    }

    rtsl_context ctx = rtslCreateContext();
    if (!ctx) {
        std::cerr << "rtslc: failed to create compiler context\n";
        return 1;
    }

    rtsl_module module = rtslCompileSource(ctx, source.data(), source.size(), input.c_str());
    if (!module) {
        std::cerr << "rtslc: compile failed: " << rtslCtxGetResult(ctx).text << "\n";
        rtslDestroyContext(ctx);
        return 1;
    }

    rtsl_module output_module = module;
    rtsl_linker linker = nullptr;
    if (link) {
        linker = rtslCreateLinker(ctx);
        if (!linker || !rtslLinkerAddModule(linker, module)) {
            std::cerr << "rtslc: linker setup failed: " << rtslCtxGetResult(ctx).text << "\n";
            rtslDestroyLinker(linker);
            rtslDestroyModule(module);
            rtslDestroyContext(ctx);
            return 1;
        }

        output_module = rtslLinkProgram(linker);
        if (!output_module) {
            std::cerr << "rtslc: link failed: " << rtslCtxGetResult(ctx).text << "\n";
            rtslDestroyLinker(linker);
            rtslDestroyModule(module);
            rtslDestroyContext(ctx);
            return 1;
        }
    }

    const rtsl_blob bytecode = rtslModuleGetBytecode(output_module);
    if (!writeFile(output, bytecode)) {
        std::cerr << "rtslc: failed to write output: " << output << "\n";
        if (output_module != module) {
            rtslDestroyModule(output_module);
        }
        rtslDestroyLinker(linker);
        rtslDestroyModule(module);
        rtslDestroyContext(ctx);
        return 1;
    }

    if (output_module != module) {
        rtslDestroyModule(output_module);
    }
    rtslDestroyLinker(linker);
    rtslDestroyModule(module);
    rtslDestroyContext(ctx);
    return 0;
}
