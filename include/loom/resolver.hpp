#pragma once

#include <loom/result.hpp>
#include <loom/cache.hpp>
#include <loom/manifest.hpp>
#include <loom/lockfile.hpp>
#include <loom/workspace.hpp>
#include <loom/local_override.hpp>
#include <loom/graph.hpp>

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace loom {

struct ResolveOptions {
    bool no_local = false;        // Suppress Loom.local overrides
    bool offline = false;         // Use cached repos only
    bool update_all = false;      // Force full re-resolution
    std::string update_package;   // Specific package to re-resolve (empty = none)
};

// Intermediate result for one resolved dependency
struct ResolvedPackage {
    std::string name;
    std::string version;
    std::string commit;           // Full SHA (empty for path deps)
    std::string ref;              // Original tag/branch/rev
    std::string source_url;       // Git URL or absolute path
    bool is_path = false;
    std::string checksum;
    std::vector<std::string> dep_names;
};

class DependencyResolver {
public:
    explicit DependencyResolver(CacheManager& cache);

    // Full resolution: manifest deps -> lockfile
    Result<LockFile> resolve(const Manifest& manifest,
                             const std::optional<LockFile>& existing_lock = {},
                             const ResolveOptions& options = {});

    // Selective update: re-resolve one package, keep rest locked
    Result<LockFile> update(const Manifest& manifest,
                            const LockFile& existing_lock,
                            const std::string& package_name,
                            const ResolveOptions& options = {});

    // Workspace resolution: unified lockfile for all members
    Result<LockFile> resolve_workspace(const Workspace& workspace,
                                       const std::optional<LockFile>& existing_lock = {},
                                       const ResolveOptions& options = {});

    // Apply local overrides to lockfile (modifies in place)
    static Status apply_overrides(LockFile& lockfile,
                                  const LocalOverrides& overrides);

    // Topological sort of locked packages
    static Result<std::vector<std::string>> topological_sort(const LockFile& lockfile);

private:
    CacheManager& cache_;

    // Core BFS resolution
    Result<std::unordered_map<std::string, ResolvedPackage>>
    resolve_deps(const std::vector<Dependency>& deps,
                 const std::optional<LockFile>& existing_lock,
                 const ResolveOptions& options,
                 const std::filesystem::path& manifest_dir);

    // Resolve a single git dependency
    Result<ResolvedPackage> resolve_git(const Dependency& dep,
                                        const LockedPackage* locked);

    // Resolve a single path dependency
    Result<ResolvedPackage> resolve_path(const Dependency& dep,
                                         const std::filesystem::path& manifest_dir);

    // Load transitive deps from a resolved package's Loom.toml
    Result<std::vector<Dependency>> load_transitive_deps(const ResolvedPackage& pkg);

    // Build LockFile from resolved map
    static LockFile build_lockfile(const Manifest& root_manifest,
                                   const std::unordered_map<std::string, ResolvedPackage>& resolved);
};

} // namespace loom
