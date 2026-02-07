#include <loom/project.hpp>
#include <loom/sha256.hpp>
#include <loom/glob.hpp>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace loom {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

Result<fs::path> find_manifest(const fs::path& start_dir) {
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
            return Result<fs::path>::ok(candidate);
        }

        fs::path parent = dir.parent_path();
        if (parent == dir) {
            // Reached filesystem root
            return LoomError{LoomError::NotFound,
                "no Loom.toml found in " + start_dir.string() + " or any parent directory"};
        }
        dir = parent;
    }
}

bool has_manifest(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / "Loom.toml", ec);
}

Result<bool> is_workspace_root(const fs::path& dir) {
    fs::path manifest_path = dir / "Loom.toml";
    std::error_code ec;
    if (!fs::exists(manifest_path, ec)) {
        return LoomError{LoomError::NotFound,
            "no Loom.toml in: " + dir.string()};
    }

    auto manifest = Manifest::load(manifest_path.string());
    if (manifest.is_err()) return std::move(manifest).error();

    return Result<bool>::ok(manifest.value().is_workspace());
}

// ---------------------------------------------------------------------------
// Project
// ---------------------------------------------------------------------------

Result<Project> Project::load(const fs::path& project_dir) {
    fs::path manifest_path = project_dir / "Loom.toml";

    // Read file contents
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return LoomError{LoomError::IO,
            "cannot open manifest: " + manifest_path.string()};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string contents = ss.str();
    file.close();

    // Compute checksum
    std::string checksum = SHA256::hash_hex(contents);

    // Parse manifest
    auto manifest = Manifest::parse(contents);
    if (manifest.is_err()) return std::move(manifest).error();

    std::error_code ec;
    fs::path abs_root = fs::canonical(project_dir, ec);
    if (ec) abs_root = fs::absolute(project_dir);

    fs::path abs_manifest = abs_root / "Loom.toml";

    Project proj;
    proj.manifest = std::move(manifest).value();
    proj.root_dir = abs_root;
    proj.manifest_path = abs_manifest;
    proj.checksum = std::move(checksum);

    return Result<Project>::ok(std::move(proj));
}

Result<Project> Project::discover(const fs::path& start_dir) {
    auto manifest_path = find_manifest(start_dir);
    if (manifest_path.is_err()) return std::move(manifest_path).error();

    return Project::load(manifest_path.value().parent_path());
}

// ---------------------------------------------------------------------------
// Source collection
// ---------------------------------------------------------------------------

// Check if a string contains glob characters
static bool is_glob_pattern(const std::string& s) {
    for (char c : s) {
        if (c == '*' || c == '?' || c == '[') return true;
    }
    return false;
}

Result<std::vector<SourceGroup>> Project::collect_source_groups(
    const TargetSet& active) const
{
    auto filtered = filter_source_groups(manifest.sources, active);

    std::vector<SourceGroup> result;
    for (auto& group : filtered) {
        SourceGroup resolved;
        resolved.target = group.target;
        resolved.include_dirs = group.include_dirs;
        resolved.defines = group.defines;

        for (const auto& file_pat : group.files) {
            if (is_glob_pattern(file_pat)) {
                auto expanded = glob_expand(file_pat, root_dir);
                if (expanded.is_err()) return std::move(expanded).error();
                for (auto& rel : expanded.value()) {
                    resolved.files.push_back(
                        (root_dir / rel).string());
                }
            } else {
                resolved.files.push_back(
                    (root_dir / file_pat).string());
            }
        }
        result.push_back(std::move(resolved));
    }

    return Result<std::vector<SourceGroup>>::ok(std::move(result));
}

Result<std::vector<std::string>> Project::collect_sources(
    const TargetSet& active) const
{
    auto groups = collect_source_groups(active);
    if (groups.is_err()) return std::move(groups).error();

    std::unordered_set<std::string> seen;
    std::vector<std::string> result;

    for (const auto& group : groups.value()) {
        for (const auto& f : group.files) {
            if (seen.insert(f).second) {
                result.push_back(f);
            }
        }
    }

    return Result<std::vector<std::string>>::ok(std::move(result));
}

} // namespace loom
