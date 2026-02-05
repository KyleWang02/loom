#pragma once

#include <string>

namespace loom {

struct LoomError {
    enum Code {
        IO,
        Parse,
        Version,
        Dependency,
        Config,
        Manifest,
        Checksum,
        Network,
        NotFound,
        Duplicate,
        Cycle,
        InvalidArg
    };

    Code code;
    std::string message;
    std::string hint;
    std::string file;
    int line = 0;

    LoomError() = default;
    LoomError(Code c, std::string msg)
        : code(c), message(std::move(msg)) {}
    LoomError(Code c, std::string msg, std::string h)
        : code(c), message(std::move(msg)), hint(std::move(h)) {}
    LoomError(Code c, std::string msg, std::string h, std::string f, int l)
        : code(c), message(std::move(msg)), hint(std::move(h)),
          file(std::move(f)), line(l) {}

    std::string format() const;
    static const char* code_name(Code c);
};

} // namespace loom
