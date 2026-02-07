#pragma once

#include <loom/result.hpp>
#include <loom/manifest.hpp>
#include <loom/target_expr.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace loom {

struct Project {
    Manifest manifest;
    std::filesystem::path root_dir;       // dir containing Loom.toml
    std::filesystem::path manifest_path;  // full path to Loom.toml
    std::string checksum;                 // SHA-256 of Loom.toml contents

    // Walk up from start_dir to find Loom.toml, then load
    static Result<Project> discover(const std::filesystem::path& start_dir);

    // Load from a specific directory (must contain Loom.toml)
    static Result<Project> load(const std::filesystem::path& project_dir);

    // Collect source files filtered by targets, resolved to absolute paths
    Result<std::vector<std::string>> collect_sources(const TargetSet& active) const;

    // Same but preserving SourceGroup structure
    Result<std::vector<SourceGroup>> collect_source_groups(const TargetSet& active) const;
};

// Walk up from start_dir to find the nearest Loom.toml, return its path
Result<std::filesystem::path> find_manifest(const std::filesystem::path& start_dir);

// Check if dir contains a Loom.toml
bool has_manifest(const std::filesystem::path& dir);

// Check if the Loom.toml in dir has a [workspace] section
Result<bool> is_workspace_root(const std::filesystem::path& dir);

} // namespace loom
