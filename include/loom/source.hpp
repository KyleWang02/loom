#pragma once

#include <loom/result.hpp>
#include <loom/version.hpp>
#include <string>
#include <optional>

namespace loom {

struct GitSource {
    std::string url;
    std::optional<std::string> tag;      // e.g., "v1.3.0"
    std::optional<std::string> version;  // semver constraint: ">=2.0.0, <3.0.0"
    std::optional<std::string> rev;      // full or short commit SHA
    std::optional<std::string> branch;   // branch name
};

struct PathSource {
    std::string path;
};

struct Dependency {
    std::string name;

    // Exactly one of these source types
    std::optional<GitSource> git;
    std::optional<PathSource> path;

    // Workspace inheritance
    bool workspace = false;  // { workspace = true }
    bool member = false;     // { member = true }

    // Validate dependency specification rules
    Result<std::monostate> validate() const;
};

} // namespace loom
