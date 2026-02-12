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

// --------------------------------------------------------------------------
// loom update [package]
// --------------------------------------------------------------------------

static Result<int> handle_update(CliArgs& global, CliArgs& cmd) {
    auto cwd = fs::current_path();

    ResolveOptions opts;
    opts.no_local = global.has("no-local");
    opts.offline = global.has("offline");

    CacheManager cache(CacheManager::default_cache_root());

    // Discover project
    auto proj_r = Project::discover(cwd);
    if (proj_r.is_err()) return std::move(proj_r).error();
    auto& project = proj_r.value();

    auto lock_path = project.root_dir / "Loom.lock";

    // Load existing lockfile (required for update)
    if (!fs::exists(lock_path)) {
        return LoomError(LoomError::NotFound,
            "no Loom.lock found -- run 'loom lock' first",
            "loom lock");
    }
    auto existing_r = LockFile::load(lock_path.string());
    if (existing_r.is_err()) return std::move(existing_r).error();
    auto existing = std::move(existing_r).value();

    DependencyResolver resolver(cache);

    auto& pos = cmd.positional();
    LockFile lockfile;

    if (!pos.empty()) {
        // Update a specific package
        const std::string& pkg_name = pos[0];

        if (!existing.find(pkg_name)) {
            return LoomError(LoomError::NotFound,
                "package '" + pkg_name + "' not found in Loom.lock",
                "check the package name and run 'loom tree' to see dependencies");
        }

        opts.update_package = pkg_name;
        auto lock_r = resolver.update(project.manifest, existing, pkg_name, opts);
        if (lock_r.is_err()) return std::move(lock_r).error();
        lockfile = std::move(lock_r).value();

        std::cout << "Updated package '" << pkg_name << "'\n";
    } else {
        // Update all packages
        opts.update_all = true;
        auto lock_r = resolver.resolve(project.manifest, existing, opts);
        if (lock_r.is_err()) return std::move(lock_r).error();
        lockfile = std::move(lock_r).value();

        std::cout << "Updated all packages\n";
    }

    auto save_r = lockfile.save(lock_path.string());
    if (save_r.is_err()) return std::move(save_r).error();

    std::cout << "Resolved " << lockfile.packages.size()
              << " package(s)\n";
    std::cout << "Wrote " << lock_path.string() << "\n";

    return Result<int>::ok(0);
}

// --------------------------------------------------------------------------
// loom fetch
// --------------------------------------------------------------------------

static Result<int> handle_fetch(CliArgs& global, CliArgs& /*cmd*/) {
    auto cwd = fs::current_path();

    ResolveOptions opts;
    opts.no_local = global.has("no-local");
    opts.offline = global.has("offline");

    CacheManager cache(CacheManager::default_cache_root());

    // Discover project or workspace
    auto ws_check = is_workspace_root(cwd);

    LockFile lockfile;
    fs::path lock_path;

    if (ws_check.is_ok() && ws_check.value()) {
        auto ws_r = Workspace::discover(cwd);
        if (ws_r.is_err()) return std::move(ws_r).error();
        auto& workspace = ws_r.value();
        lock_path = workspace.root_dir() / "Loom.lock";

        // Resolve if no lockfile exists
        if (!fs::exists(lock_path)) {
            DependencyResolver resolver(cache);
            auto lock_r = resolver.resolve_workspace(workspace, {}, opts);
            if (lock_r.is_err()) return std::move(lock_r).error();
            lockfile = std::move(lock_r).value();

            auto save_r = lockfile.save(lock_path.string());
            if (save_r.is_err()) return std::move(save_r).error();
        } else {
            auto lock_r = LockFile::load(lock_path.string());
            if (lock_r.is_err()) return std::move(lock_r).error();
            lockfile = std::move(lock_r).value();
        }
    } else {
        auto proj_r = Project::discover(cwd);
        if (proj_r.is_err()) return std::move(proj_r).error();
        auto& project = proj_r.value();
        lock_path = project.root_dir / "Loom.lock";

        // Resolve if no lockfile exists
        if (!fs::exists(lock_path)) {
            DependencyResolver resolver(cache);
            auto lock_r = resolver.resolve(project.manifest, {}, opts);
            if (lock_r.is_err()) return std::move(lock_r).error();
            lockfile = std::move(lock_r).value();

            auto save_r = lockfile.save(lock_path.string());
            if (save_r.is_err()) return std::move(save_r).error();
        } else {
            auto lock_r = LockFile::load(lock_path.string());
            if (lock_r.is_err()) return std::move(lock_r).error();
            lockfile = std::move(lock_r).value();
        }
    }

    // Ensure all checkouts exist
    size_t fetched = 0;
    for (const auto& pkg : lockfile.packages) {
        // Only fetch git sources (path deps are already local)
        if (pkg.source.rfind("git+", 0) == 0) {
            std::string url = pkg.source.substr(4); // strip "git+" prefix
            auto co_r = cache.ensure_checkout(pkg.name, url, pkg.version, pkg.commit);
            if (co_r.is_err()) {
                log::error("failed to fetch '%s': %s",
                           pkg.name.c_str(), co_r.error().message.c_str());
                return std::move(co_r).error();
            }
            ++fetched;
        }
    }

    std::cout << "Fetched " << fetched << " package(s), "
              << lockfile.packages.size() << " total in lockfile\n";

    return Result<int>::ok(0);
}

// --------------------------------------------------------------------------
// Registration
// --------------------------------------------------------------------------

void register_update(CliParser& cli) {
    // loom update [package]
    {
        Command cmd;
        cmd.name = "update";
        cmd.summary = "Update dependencies in Loom.lock";
        cmd.description = "Re-resolves dependencies and updates Loom.lock. If a package "
                          "name is given, only that package is updated while keeping the "
                          "rest locked. Without arguments, all packages are re-resolved.";
        cmd.usage = "loom update [package]";
        cmd.group = "Dependencies";
        cmd.flags = {};
        cmd.handler = handle_update;
        cli.add_command(std::move(cmd));
    }

    // loom fetch
    {
        Command cmd;
        cmd.name = "fetch";
        cmd.summary = "Fetch all dependencies into the local cache";
        cmd.description = "Ensures a Loom.lock exists (resolving if needed), then downloads "
                          "all git dependencies so that working-tree checkouts are available "
                          "locally. Path dependencies are skipped since they are already local.";
        cmd.usage = "loom fetch";
        cmd.group = "Dependencies";
        cmd.flags = {};
        cmd.handler = handle_fetch;
        cli.add_command(std::move(cmd));
    }
}

} // namespace loom
