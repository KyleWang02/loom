#include <loom/workspace.hpp>
#include <loom/glob.hpp>
#include <loom/project.hpp>
#include <unordered_set>
#include <algorithm>

namespace loom {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Status Workspace::expand_member_globs() {
    if (!root_manifest_.workspace.has_value()) {
        return LoomError{LoomError::Manifest,
            "not a workspace manifest"};
    }

    const auto& ws = root_manifest_.workspace.value();
    std::unordered_set<std::string> found_dirs;  // relative dir paths

    // Collect all directories under root_dir that contain Loom.toml
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(root_dir_, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        if (ec) continue;

        // Skip hidden directories and the root itself
        auto rel = fs::relative(entry.path(), root_dir_, ec);
        if (ec) continue;
        std::string rel_str = rel.string();
        // Normalize to forward slashes
        for (char& c : rel_str) {
            if (c == '\\') c = '/';
        }

        // Skip if this dir doesn't have a Loom.toml
        if (!fs::exists(entry.path() / "Loom.toml", ec)) continue;

        // Check if it matches any member pattern
        bool matches = false;
        for (const auto& pattern : ws.members) {
            if (glob_match(pattern, rel_str)) {
                matches = true;
                break;
            }
        }
        if (!matches) continue;

        // Check if excluded
        bool excluded = false;
        for (const auto& pattern : ws.exclude) {
            if (glob_match(pattern, rel_str)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        found_dirs.insert(rel_str);
    }

    // Load each member manifest
    for (const auto& rel_dir : found_dirs) {
        fs::path member_dir = root_dir_ / rel_dir;
        fs::path manifest_path = member_dir / "Loom.toml";

        auto manifest = Manifest::load(manifest_path.string());
        if (manifest.is_err()) return std::move(manifest).error();

        WorkspaceMember member;
        member.name = manifest.value().package.name;
        member.version = manifest.value().package.version;
        member.manifest_path = manifest_path;
        member.root_dir = member_dir;
        member.manifest = std::move(manifest).value();

        members_.push_back(std::move(member));
    }

    // Sort members by name for deterministic ordering
    std::sort(members_.begin(), members_.end(),
        [](const WorkspaceMember& a, const WorkspaceMember& b) {
            return a.name < b.name;
        });

    return ok_status();
}

// ---------------------------------------------------------------------------
// Static factory methods
// ---------------------------------------------------------------------------

Result<Workspace> Workspace::load(const fs::path& workspace_root) {
    fs::path manifest_path = workspace_root / "Loom.toml";

    auto manifest = Manifest::load(manifest_path.string());
    if (manifest.is_err()) return std::move(manifest).error();

    if (!manifest.value().is_workspace()) {
        return LoomError{LoomError::Manifest,
            "not a workspace: " + manifest_path.string(),
            "add a [workspace] section to make this a workspace root"};
    }

    Workspace ws;

    std::error_code ec;
    ws.root_dir_ = fs::canonical(workspace_root, ec);
    if (ec) ws.root_dir_ = fs::absolute(workspace_root);

    ws.root_manifest_ = std::move(manifest).value();

    auto status = ws.expand_member_globs();
    if (status.is_err()) return std::move(status).error();

    auto validate_status = ws.validate();
    if (validate_status.is_err()) return std::move(validate_status).error();

    return Result<Workspace>::ok(std::move(ws));
}

Result<Workspace> Workspace::discover(const fs::path& start_dir) {
    std::error_code ec;
    fs::path dir = fs::canonical(start_dir, ec);
    if (ec) {
        dir = fs::absolute(start_dir, ec);
        if (ec) {
            return LoomError{LoomError::IO,
                "cannot resolve path: " + start_dir.string()};
        }
    }

    while (true) {
        fs::path candidate = dir / "Loom.toml";
        if (fs::exists(candidate, ec)) {
            // Check if this is a workspace manifest
            auto manifest = Manifest::load(candidate.string());
            if (manifest.is_ok() && manifest.value().is_workspace()) {
                return Workspace::load(dir);
            }
            // Not a workspace, keep walking up
        }

        fs::path parent = dir.parent_path();
        if (parent == dir) {
            return LoomError{LoomError::NotFound,
                "no workspace root found from: " + start_dir.string()};
        }
        dir = parent;
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const std::vector<WorkspaceMember>& Workspace::members() const {
    return members_;
}

size_t Workspace::member_count() const {
    return members_.size();
}

const WorkspaceMember* Workspace::find_member(const std::string& name) const {
    for (const auto& m : members_) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

const WorkspaceMember* Workspace::member_for_path(const fs::path& path) const {
    std::error_code ec;
    fs::path abs_path = fs::canonical(path, ec);
    if (ec) abs_path = fs::absolute(path);

    for (const auto& m : members_) {
        fs::path abs_member = fs::canonical(m.root_dir, ec);
        if (ec) abs_member = fs::absolute(m.root_dir);

        // Check if abs_path starts with the member's root
        auto rel = fs::relative(abs_path, abs_member, ec);
        if (ec) continue;
        std::string rel_str = rel.string();
        // If relative path doesn't start with "..", it's inside the member
        if (!rel_str.empty() && rel_str.substr(0, 2) != "..") {
            return &m;
        }
    }
    return nullptr;
}

bool Workspace::is_virtual() const {
    return root_manifest_.package.name.empty();
}

const Manifest& Workspace::root_manifest() const {
    return root_manifest_;
}

const fs::path& Workspace::root_dir() const {
    return root_dir_;
}

// ---------------------------------------------------------------------------
// resolve_targets
// ---------------------------------------------------------------------------

Result<std::vector<const WorkspaceMember*>> Workspace::resolve_targets(
    const std::vector<std::string>& pkg_flags,
    bool all,
    const fs::path& cwd) const
{
    std::vector<const WorkspaceMember*> result;

    if (!pkg_flags.empty()) {
        // Explicit -p <name> flags
        for (const auto& name : pkg_flags) {
            auto* m = find_member(name);
            if (!m) {
                return LoomError{LoomError::NotFound,
                    "no workspace member named '" + name + "'"};
            }
            result.push_back(m);
        }
        return Result<std::vector<const WorkspaceMember*>>::ok(std::move(result));
    }

    if (all) {
        for (const auto& m : members_) {
            result.push_back(&m);
        }
        return Result<std::vector<const WorkspaceMember*>>::ok(std::move(result));
    }

    // Check if there are default-members specified
    // default-members uses relative directory paths (e.g., "soc/top"), not package names
    if (root_manifest_.workspace.has_value() &&
        !root_manifest_.workspace->default_members.empty())
    {
        for (const auto& dm : root_manifest_.workspace->default_members) {
            std::error_code dm_ec;
            fs::path dm_abs = fs::canonical(root_dir_ / dm, dm_ec);
            if (dm_ec) dm_abs = fs::absolute(root_dir_ / dm);
            for (const auto& m : members_) {
                fs::path m_abs = fs::canonical(m.root_dir, dm_ec);
                if (dm_ec) m_abs = fs::absolute(m.root_dir);
                if (m_abs == dm_abs) {
                    result.push_back(&m);
                    break;
                }
            }
        }
        if (!result.empty()) {
            return Result<std::vector<const WorkspaceMember*>>::ok(std::move(result));
        }
    }

    // Fall back to the member containing cwd
    auto* m = member_for_path(cwd);
    if (m) {
        result.push_back(m);
        return Result<std::vector<const WorkspaceMember*>>::ok(std::move(result));
    }

    // No member found, return all members
    for (const auto& m : members_) {
        result.push_back(&m);
    }
    return Result<std::vector<const WorkspaceMember*>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Dependency resolution
// ---------------------------------------------------------------------------

Result<Dependency> Workspace::resolve_workspace_dep(const std::string& dep_name) const {
    if (!root_manifest_.workspace.has_value()) {
        return LoomError{LoomError::Dependency,
            "not a workspace, cannot resolve workspace dependency: " + dep_name};
    }

    for (const auto& dep : root_manifest_.workspace->dependencies) {
        if (dep.name == dep_name) {
            return Result<Dependency>::ok(dep);
        }
    }

    return LoomError{LoomError::Dependency,
        "workspace dependency '" + dep_name + "' not found in [workspace.dependencies]"};
}

Result<Dependency> Workspace::resolve_member_dep(const std::string& dep_name) const {
    const auto* m = find_member(dep_name);
    if (!m) {
        return LoomError{LoomError::Dependency,
            "member dependency '" + dep_name + "' not found in workspace members"};
    }

    Dependency dep;
    dep.name = dep_name;
    dep.path = PathSource{m->root_dir.string()};
    return Result<Dependency>::ok(std::move(dep));
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

Config Workspace::effective_config(const WorkspaceMember& member) const {
    // Load global config
    std::optional<Config> global;
    auto gpath = global_config_path();
    if (!gpath.empty()) {
        auto gc = Config::load(gpath);
        if (gc.is_ok()) global = std::move(gc).value();
    }

    // Extract workspace-level config from root manifest
    std::optional<Config> workspace_cfg;
    {
        Config wc;
        wc.lint = root_manifest_.lint;
        wc.build = root_manifest_.build;
        wc.targets = root_manifest_.targets;
        // Mark build fields as set if workspace has a [build] section
        // (they're always potentially set from manifest)
        wc.build_pre_lint_set = true;
        wc.build_lint_fatal_set = true;
        workspace_cfg = std::move(wc);
    }

    // Extract member-level config
    std::optional<Config> member_cfg;
    {
        Config mc;
        mc.lint = member.manifest.lint;
        mc.build = member.manifest.build;
        mc.targets = member.manifest.targets;
        mc.build_pre_lint_set = true;
        mc.build_lint_fatal_set = true;
        member_cfg = std::move(mc);
    }

    return Config::effective(global, workspace_cfg, member_cfg);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

Status Workspace::validate() const {
    // Check for duplicate member names
    std::unordered_set<std::string> names;
    for (const auto& m : members_) {
        if (!names.insert(m.name).second) {
            return LoomError{LoomError::Duplicate,
                "duplicate workspace member name: " + m.name};
        }
    }

    // Check for nested workspaces
    for (const auto& m : members_) {
        if (m.manifest.is_workspace()) {
            return LoomError{LoomError::Manifest,
                "member '" + m.name + "' is itself a workspace, nested workspaces not allowed"};
        }
    }

    // Check that no member has its own Loom.lock
    std::error_code ec;
    for (const auto& m : members_) {
        if (fs::exists(m.root_dir / "Loom.lock", ec)) {
            return LoomError{LoomError::Manifest,
                "member '" + m.name + "' has its own Loom.lock, "
                "only the workspace root should have a lockfile"};
        }
    }

    // Validate workspace=true deps resolve
    for (const auto& m : members_) {
        for (const auto& dep : m.manifest.dependencies) {
            if (dep.workspace) {
                if (!root_manifest_.workspace.has_value()) continue;
                bool found = false;
                for (const auto& wd : root_manifest_.workspace->dependencies) {
                    if (wd.name == dep.name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return LoomError{LoomError::Dependency,
                        "member '" + m.name + "' depends on workspace dep '" +
                        dep.name + "' which is not in [workspace.dependencies]"};
                }
            }

            if (dep.member) {
                if (!find_member(dep.name)) {
                    return LoomError{LoomError::Dependency,
                        "member '" + m.name + "' depends on member '" +
                        dep.name + "' which is not a workspace member"};
                }
            }
        }
    }

    return ok_status();
}

} // namespace loom
