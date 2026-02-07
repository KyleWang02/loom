#pragma once

#include <loom/result.hpp>
#include <string>
#include <unordered_map>
#include <filesystem>

namespace loom {

struct OverrideSource {
    enum class Kind { Path, Git };
    Kind kind;
    std::string path;     // Kind::Path: local directory path
    std::string url;      // Kind::Git: git URL
    std::string branch;   // optional git ref
    std::string tag;      // optional git ref
    std::string rev;      // optional git ref
};

struct LocalOverrides {
    std::unordered_map<std::string, OverrideSource> overrides;

    // Load from a Loom.local file
    static Result<LocalOverrides> load(const std::filesystem::path& local_file);

    // Parse from TOML string
    static Result<LocalOverrides> parse(const std::string& toml_str);

    bool has_override(const std::string& name) const;
    const OverrideSource* get_override(const std::string& name) const;
    size_t count() const;
    bool empty() const;

    // Validate overrides (path dirs exist, git URLs non-empty, etc.)
    Status validate() const;

    // Warn about active overrides via loom::log::warn
    void warn_active() const;
};

// Look for Loom.local in project_root. Returns empty LocalOverrides if not found.
Result<LocalOverrides> discover_local_overrides(const std::filesystem::path& project_root);

// Check if overrides should be suppressed (--no-local flag or LOOM_NO_LOCAL=1 env)
bool should_suppress_overrides(bool no_local_flag);

} // namespace loom
