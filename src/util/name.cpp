#include <loom/name.hpp>
#include <algorithm>
#include <cctype>

namespace loom {

Result<PkgName> PkgName::parse(const std::string& raw) {
    if (raw.empty()) {
        return LoomError{LoomError::InvalidArg, "empty package name"};
    }

    if (!std::isalpha(static_cast<unsigned char>(raw[0]))) {
        return LoomError{LoomError::InvalidArg,
            "invalid package name '" + raw + "'",
            "package names must start with a letter"};
    }

    for (size_t i = 1; i < raw.size(); ++i) {
        char c = raw[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '_' && c != '-') {
            return LoomError{LoomError::InvalidArg,
                "invalid character '" + std::string(1, c) +
                "' in package name '" + raw + "'",
                "allowed: [a-zA-Z0-9_-]"};
        }
    }

    PkgName name;
    name.raw_ = raw;
    // Normalize: lowercase, hyphens to underscores
    name.normalized_ = raw;
    std::transform(name.normalized_.begin(), name.normalized_.end(),
                   name.normalized_.begin(),
                   [](char c) -> char {
                       if (c == '-') return '_';
                       return static_cast<char>(
                           std::tolower(static_cast<unsigned char>(c)));
                   });

    return Result<PkgName>::ok(std::move(name));
}

const std::string& PkgName::raw() const { return raw_; }
const std::string& PkgName::normalized() const { return normalized_; }

bool PkgName::operator==(const PkgName& o) const {
    return normalized_ == o.normalized_;
}

bool PkgName::operator!=(const PkgName& o) const {
    return !(*this == o);
}

} // namespace loom
