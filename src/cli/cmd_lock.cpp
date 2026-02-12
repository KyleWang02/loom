#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/workspace.hpp>
#include <loom/resolver.hpp>
#include <loom/cache.hpp>
#include <loom/lockfile.hpp>
#include <loom/local_override.hpp>
#include <iostream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_lock(CliArgs& global, CliArgs& /*cmd*/) {
    auto cwd = fs::current_path();

    // Build ResolveOptions from global flags
    ResolveOptions opts;
    opts.no_local = global.has("no-local");
    opts.offline = global.has("offline");

    // Initialize cache manager
    CacheManager cache(CacheManager::default_cache_root());

    // Check if this is a workspace root
    auto ws_check = is_workspace_root(cwd);

    if (ws_check.is_ok() && ws_check.value()) {
        // Workspace resolution
        auto ws_r = Workspace::discover(cwd);
        if (ws_r.is_err()) return std::move(ws_r).error();
        auto& workspace = ws_r.value();

        // Warn about Loom.local if present (but lock never uses it)
        auto local_r = discover_local_overrides(workspace.root_dir());
        if (local_r.is_ok() && !local_r.value().empty()) {
            log::warn("Loom.local detected with %zu override(s) -- ignored during lock",
                      local_r.value().count());
        }

        // Load existing lockfile if present
        std::optional<LockFile> existing;
        auto lock_path = workspace.root_dir() / "Loom.lock";
        if (fs::exists(lock_path)) {
            auto lock_r = LockFile::load(lock_path.string());
            if (lock_r.is_ok()) {
                existing = std::move(lock_r).value();
            }
        }

        DependencyResolver resolver(cache);
        auto lock_r = resolver.resolve_workspace(workspace, existing, opts);
        if (lock_r.is_err()) return std::move(lock_r).error();
        auto& lockfile = lock_r.value();

        auto save_r = lockfile.save(lock_path.string());
        if (save_r.is_err()) return std::move(save_r).error();

        std::cout << "Resolved " << lockfile.packages.size()
                  << " package(s) for workspace\n";
        std::cout << "Wrote " << lock_path.string() << "\n";

        return Result<int>::ok(0);
    }

    // Single project resolution
    auto proj_r = Project::discover(cwd);
    if (proj_r.is_err()) return std::move(proj_r).error();
    auto& project = proj_r.value();

    // Warn about Loom.local if present (but lock never uses it)
    auto local_r = discover_local_overrides(project.root_dir);
    if (local_r.is_ok() && !local_r.value().empty()) {
        log::warn("Loom.local detected with %zu override(s) -- ignored during lock",
                  local_r.value().count());
    }

    // Load existing lockfile if present
    std::optional<LockFile> existing;
    auto lock_path = project.root_dir / "Loom.lock";
    if (fs::exists(lock_path)) {
        auto lock_r = LockFile::load(lock_path.string());
        if (lock_r.is_ok()) {
            existing = std::move(lock_r).value();
        }
    }

    DependencyResolver resolver(cache);
    auto lock_r = resolver.resolve(project.manifest, existing, opts);
    if (lock_r.is_err()) return std::move(lock_r).error();
    auto& lockfile = lock_r.value();

    auto save_r = lockfile.save(lock_path.string());
    if (save_r.is_err()) return std::move(save_r).error();

    std::cout << "Resolved " << lockfile.packages.size()
              << " package(s)\n";
    std::cout << "Wrote " << lock_path.string() << "\n";

    return Result<int>::ok(0);
}

void register_lock(CliParser& cli) {
    Command cmd;
    cmd.name = "lock";
    cmd.summary = "Resolve dependencies and create Loom.lock";
    cmd.description = "Resolves all dependencies declared in Loom.toml and writes a "
                      "deterministic Loom.lock file. In a workspace, resolves all "
                      "members into a single lockfile. Loom.local overrides are ignored "
                      "during lock to keep the lockfile reproducible.";
    cmd.usage = "loom lock";
    cmd.group = "Dependencies";
    cmd.flags = {};
    cmd.handler = handle_lock;
    cli.add_command(std::move(cmd));
}

} // namespace loom
