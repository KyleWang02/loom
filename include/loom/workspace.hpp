#pragma once

#include <loom/result.hpp>
#include <loom/manifest.hpp>
#include <loom/config.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace loom {

struct WorkspaceMember {
    std::string name;
    std::string version;
    std::filesystem::path manifest_path;
    std::filesystem::path root_dir;
    Manifest manifest;
};

class Workspace {
public:
    // Walk up from start_dir to find a workspace root (Loom.toml with [workspace])
    static Result<Workspace> discover(const std::filesystem::path& start_dir);

    // Load from a specific workspace root directory
    static Result<Workspace> load(const std::filesystem::path& workspace_root);

    const std::vector<WorkspaceMember>& members() const;
    size_t member_count() const;

    // Find member by package name
    const WorkspaceMember* find_member(const std::string& name) const;

    // Find the member whose root_dir contains the given path
    const WorkspaceMember* member_for_path(const std::filesystem::path& path) const;

    // Resolve which members to operate on from CLI flags:
    // - pkg_flags: -p <name> flags
    // - all: --all flag
    // - cwd: current working directory (for default selection)
    Result<std::vector<const WorkspaceMember*>> resolve_targets(
        const std::vector<std::string>& pkg_flags,
        bool all,
        const std::filesystem::path& cwd) const;

    // Resolve a { workspace = true } dependency from [workspace.dependencies]
    Result<Dependency> resolve_workspace_dep(const std::string& dep_name) const;

    // Resolve a { member = true } dependency to a path dep pointing at the member
    Result<Dependency> resolve_member_dep(const std::string& dep_name) const;

    // Build effective config for a member (global -> workspace -> member layers)
    Config effective_config(const WorkspaceMember& member) const;

    // Validate workspace structure
    Status validate() const;

    const Manifest& root_manifest() const;
    const std::filesystem::path& root_dir() const;

    // Virtual workspace: has [workspace] but no [package] section
    bool is_virtual() const;

private:
    Manifest root_manifest_;
    std::filesystem::path root_dir_;
    std::vector<WorkspaceMember> members_;

    // Expand member glob patterns into actual member directories
    Status expand_member_globs();
};

} // namespace loom
