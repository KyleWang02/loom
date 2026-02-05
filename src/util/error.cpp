#include <loom/error.hpp>

namespace loom {

const char* LoomError::code_name(Code c) {
    switch (c) {
        case IO:         return "IO";
        case Parse:      return "Parse";
        case Version:    return "Version";
        case Dependency: return "Dependency";
        case Config:     return "Config";
        case Manifest:   return "Manifest";
        case Checksum:   return "Checksum";
        case Network:    return "Network";
        case NotFound:   return "NotFound";
        case Duplicate:  return "Duplicate";
        case Cycle:      return "Cycle";
        case InvalidArg: return "InvalidArg";
    }
    return "Unknown";
}

std::string LoomError::format() const {
    std::string result = "error[";
    result += code_name(code);
    result += "]: ";
    result += message;

    if (!hint.empty()) {
        result += "\n  hint: ";
        result += hint;
    }

    if (!file.empty()) {
        result += "\n  --> ";
        result += file;
        if (line > 0) {
            result += ":";
            result += std::to_string(line);
        }
    }

    return result;
}

} // namespace loom
