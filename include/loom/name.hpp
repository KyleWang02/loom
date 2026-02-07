#pragma once

#include <loom/result.hpp>
#include <string>

namespace loom {

// Package name: [a-zA-Z][a-zA-Z0-9_-]*
// Normalized form: lowercase with hyphens converted to underscores
struct PkgName {
    static Result<PkgName> parse(const std::string& raw);

    const std::string& raw() const;
    const std::string& normalized() const;

    bool operator==(const PkgName& o) const;
    bool operator!=(const PkgName& o) const;

private:
    std::string raw_;
    std::string normalized_;
};

} // namespace loom
