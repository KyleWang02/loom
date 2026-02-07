#include <loom/resolver.hpp>
#include <loom/log.hpp>

#include <queue>
#include <algorithm>

namespace loom {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DependencyResolver::DependencyResolver(CacheManager& cache)
    : cache_(cache) {}

// ---------------------------------------------------------------------------
// resolve()
// ---------------------------------------------------------------------------

Result<LockFile> DependencyResolver::resolve(
    const Manifest& manifest,
    const std::optional<LockFile>& existing_lock,
    const ResolveOptions& options)
{
    if (options.offline) {
        cache_.git().set_offline(true);
    }

    // If valid lockfile exists and not stale and not forcing update, reuse it
    if (existing_lock.has_value() && !options.update_all &&
        !existing_lock->is_stale(manifest.dependencies))
    {
        log::debug("lockfile is up-to-date, reusing");
        return Result<LockFile>::ok(*existing_lock);
    }

    // Determine manifest directory (use cwd as fallback)
    fs::path manifest_dir = fs::current_path();

    auto resolved = resolve_deps(manifest.dependencies, existing_lock,
                                 options, manifest_dir);
    if (resolved.is_err()) return std::move(resolved).error();

    return Result<LockFile>::ok(build_lockfile(manifest, resolved.value()));
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------

Result<LockFile> DependencyResolver::update(
    const Manifest& manifest,
    const LockFile& existing_lock,
    const std::string& package_name,
    const ResolveOptions& options)
{
    if (options.offline) {
        cache_.git().set_offline(true);
    }

    // Verify the package exists in the lockfile
    if (!existing_lock.find(package_name)) {
        return LoomError{LoomError::NotFound,
            "package '" + package_name + "' not found in lockfile"};
    }

    // Build a modified lockfile without the target package
    LockFile modified_lock = existing_lock;
    modified_lock.packages.erase(
        std::remove_if(modified_lock.packages.begin(),
                       modified_lock.packages.end(),
                       [&](const LockedPackage& p) {
                           return p.name == package_name;
                       }),
        modified_lock.packages.end());

    fs::path manifest_dir = fs::current_path();

    auto resolved = resolve_deps(manifest.dependencies,
                                 std::optional<LockFile>(modified_lock),
                                 options, manifest_dir);
    if (resolved.is_err()) return std::move(resolved).error();

    return Result<LockFile>::ok(build_lockfile(manifest, resolved.value()));
}

// ---------------------------------------------------------------------------
// resolve_workspace()
// ---------------------------------------------------------------------------

Result<LockFile> DependencyResolver::resolve_workspace(
    const Workspace& workspace,
    const std::optional<LockFile>& existing_lock,
    const ResolveOptions& options)
{
    if (options.offline) {
        cache_.git().set_offline(true);
    }

    // Collect all unique deps from all workspace members
    std::vector<Dependency> all_deps;
    std::unordered_map<std::string, std::string> dep_sources; // name -> source for conflict check

    for (const auto& member : workspace.members()) {
        for (const auto& dep : member.manifest.dependencies) {
            Dependency resolved_dep = dep;

            // Expand workspace=true deps
            if (dep.workspace) {
                auto ws_dep = workspace.resolve_workspace_dep(dep.name);
                if (ws_dep.is_err()) return std::move(ws_dep).error();
                resolved_dep = std::move(ws_dep).value();
            }

            // Expand member=true deps
            if (dep.member) {
                auto mem_dep = workspace.resolve_member_dep(dep.name);
                if (mem_dep.is_err()) return std::move(mem_dep).error();
                resolved_dep = std::move(mem_dep).value();
            }

            // Check for conflicts (same name, different source)
            std::string source_key;
            if (resolved_dep.git) {
                source_key = "git+" + resolved_dep.git->url;
            } else if (resolved_dep.path) {
                source_key = "path+" + resolved_dep.path->path;
            }

            auto it = dep_sources.find(resolved_dep.name);
            if (it != dep_sources.end()) {
                if (it->second != source_key) {
                    return LoomError{LoomError::Dependency,
                        "conflicting sources for dependency '" + resolved_dep.name +
                        "': '" + it->second + "' vs '" + source_key + "'"};
                }
                continue; // Already have this dep
            }

            dep_sources[resolved_dep.name] = source_key;
            all_deps.push_back(std::move(resolved_dep));
        }
    }

    // Also include root manifest deps if any (non-virtual workspace)
    if (!workspace.is_virtual()) {
        for (const auto& dep : workspace.root_manifest().dependencies) {
            if (dep_sources.count(dep.name)) continue;
            std::string source_key;
            if (dep.git) source_key = "git+" + dep.git->url;
            else if (dep.path) source_key = "path+" + dep.path->path;
            dep_sources[dep.name] = source_key;
            all_deps.push_back(dep);
        }
    }

    fs::path manifest_dir = workspace.root_dir();

    auto resolved = resolve_deps(all_deps, existing_lock, options, manifest_dir);
    if (resolved.is_err()) return std::move(resolved).error();

    return Result<LockFile>::ok(
        build_lockfile(workspace.root_manifest(), resolved.value()));
}

// ---------------------------------------------------------------------------
// resolve_deps() — BFS core
// ---------------------------------------------------------------------------

Result<std::unordered_map<std::string, ResolvedPackage>>
DependencyResolver::resolve_deps(
    const std::vector<Dependency>& deps,
    const std::optional<LockFile>& existing_lock,
    const ResolveOptions& options,
    const fs::path& manifest_dir)
{
    std::unordered_map<std::string, ResolvedPackage> resolved;

    // Queue entries: (dependency, manifest_dir for path resolution)
    struct QueueEntry {
        Dependency dep;
        fs::path context_dir;
    };
    std::queue<QueueEntry> queue;

    // Seed queue with root dependencies
    for (const auto& dep : deps) {
        queue.push({dep, manifest_dir});
    }

    while (!queue.empty()) {
        auto entry = std::move(queue.front());
        queue.pop();

        const auto& dep = entry.dep;

        // Skip if already resolved (first-to-resolve wins — BFS guarantees
        // closest-to-root wins)
        if (resolved.count(dep.name)) continue;

        // Skip workspace/member deps — these should have been expanded
        if (dep.workspace || dep.member) {
            return LoomError{LoomError::Dependency,
                "unexpected workspace/member dependency '" + dep.name +
                "' in resolution; these must be expanded before resolving"};
        }

        // Check for lock hint
        const LockedPackage* locked = nullptr;
        if (existing_lock.has_value()) {
            locked = existing_lock->find(dep.name);

            // If we're updating a specific package, skip lock hint for it
            if (locked && !options.update_package.empty() &&
                dep.name == options.update_package) {
                locked = nullptr;
            }
        }

        // Resolve based on source type
        ResolvedPackage pkg;
        if (dep.git) {
            auto r = resolve_git(dep, locked);
            if (r.is_err()) return std::move(r).error();
            pkg = std::move(r).value();
        } else if (dep.path) {
            auto r = resolve_path(dep, entry.context_dir);
            if (r.is_err()) return std::move(r).error();
            pkg = std::move(r).value();
        } else {
            return LoomError{LoomError::Dependency,
                "dependency '" + dep.name + "' has no source (git or path)"};
        }

        // Load transitive dependencies
        auto trans = load_transitive_deps(pkg);
        if (trans.is_err()) return std::move(trans).error();

        // Record dep names
        for (const auto& td : trans.value()) {
            pkg.dep_names.push_back(td.name);
        }

        // Determine context dir for transitive deps
        fs::path trans_dir;
        if (pkg.is_path) {
            trans_dir = pkg.source_url;
        } else {
            // For git deps, use the checkout directory
            trans_dir = cache_.checkout_path(pkg.name, pkg.source_url,
                                             pkg.version, pkg.commit);
        }

        // Store resolved package
        resolved[dep.name] = std::move(pkg);

        // Enqueue transitive deps
        for (auto& td : trans.value()) {
            if (!resolved.count(td.name)) {
                queue.push({std::move(td), trans_dir});
            }
        }
    }

    // Validate no cycles via topological sort
    {
        GraphMap<> graph;
        for (const auto& [name, pkg] : resolved) {
            graph.add_node(name);
            for (const auto& dep_name : pkg.dep_names) {
                graph.add_edge(name, dep_name);
            }
        }
        auto sort_result = graph.topological_sort();
        if (sort_result.is_err()) {
            return LoomError{LoomError::Cycle,
                "dependency cycle detected in resolved packages"};
        }
    }

    return Result<std::unordered_map<std::string, ResolvedPackage>>::ok(
        std::move(resolved));
}

// ---------------------------------------------------------------------------
// resolve_git()
// ---------------------------------------------------------------------------

Result<ResolvedPackage> DependencyResolver::resolve_git(
    const Dependency& dep,
    const LockedPackage* locked)
{
    const auto& gs = *dep.git;

    // If we have a valid lock hint with matching source, reuse it
    if (locked) {
        std::string expected_source = "git+" + gs.url;
        if (locked->source == expected_source && !locked->commit.empty()) {
            log::debug("reusing locked %s @ %s", dep.name.c_str(),
                       locked->commit.substr(0, 7).c_str());

            ResolvedPackage pkg;
            pkg.name = dep.name;
            pkg.version = locked->version;
            pkg.commit = locked->commit;
            pkg.ref = locked->ref;
            pkg.source_url = gs.url;
            pkg.is_path = false;
            pkg.checksum = locked->checksum;
            return Result<ResolvedPackage>::ok(std::move(pkg));
        }
    }

    // Ensure bare repo
    auto bare_result = cache_.ensure_bare_repo(dep.name, gs.url);
    if (bare_result.is_err()) return std::move(bare_result).error();
    const auto& bare_path = bare_result.value();

    std::string commit;
    std::string ref;
    std::string version_str;

    if (gs.tag) {
        // Tag: resolve to commit
        ref = *gs.tag;
        auto r = cache_.git().resolve_ref(bare_path, ref);
        if (r.is_err()) return std::move(r).error();
        commit = r.value();

        // Try to parse version from tag
        std::string tag_str = ref;
        if (!tag_str.empty() && tag_str[0] == 'v') tag_str = tag_str.substr(1);
        auto ver = Version::parse(tag_str);
        version_str = ver.is_ok() ? ver.value().to_string() : ref;

    } else if (gs.version) {
        // Version constraint: find best matching tag
        auto req = VersionReq::parse(*gs.version);
        if (req.is_err()) return std::move(req).error();

        auto ls_output = cache_.git().ls_remote(gs.url);
        if (ls_output.is_err()) return std::move(ls_output).error();

        auto tags = parse_ls_remote_tags(ls_output.value());
        if (tags.is_err()) return std::move(tags).error();

        auto best = resolve_version_from_tags(tags.value(), req.value());
        if (best.is_err()) return std::move(best).error();

        ref = best.value().name;
        commit = best.value().commit;
        version_str = best.value().version.to_string();

        // Resolve the actual commit SHA from the bare repo
        // (ls-remote may return short SHAs or tag object SHAs)
        auto full_sha = cache_.git().resolve_ref(bare_path, ref);
        if (full_sha.is_ok()) {
            commit = full_sha.value();
        }

    } else if (gs.rev) {
        // Explicit rev
        ref = *gs.rev;
        auto r = cache_.git().resolve_ref(bare_path, ref);
        if (r.is_err()) return std::move(r).error();
        commit = r.value();
        version_str = commit.substr(0, 7);

    } else if (gs.branch) {
        // Branch: resolve to HEAD commit
        ref = *gs.branch;
        auto r = cache_.git().resolve_ref(bare_path, "refs/heads/" + ref);
        if (r.is_err()) {
            // Fall back to bare ref name
            r = cache_.git().resolve_ref(bare_path, ref);
            if (r.is_err()) return std::move(r).error();
        }
        commit = r.value();
        version_str = ref + "-" + commit.substr(0, 7);

    } else {
        return LoomError{LoomError::Dependency,
            "git dependency '" + dep.name +
            "' must specify tag, version, rev, or branch"};
    }

    // Ensure checkout
    auto co_result = cache_.ensure_checkout(dep.name, gs.url,
                                             version_str, commit);
    if (co_result.is_err()) return std::move(co_result).error();

    // Compute checksum
    auto checksum = cache_.compute_checksum(co_result.value());
    if (checksum.is_err()) return std::move(checksum).error();

    ResolvedPackage pkg;
    pkg.name = dep.name;
    pkg.version = version_str;
    pkg.commit = commit;
    pkg.ref = ref;
    pkg.source_url = gs.url;
    pkg.is_path = false;
    pkg.checksum = checksum.value();

    return Result<ResolvedPackage>::ok(std::move(pkg));
}

// ---------------------------------------------------------------------------
// resolve_path()
// ---------------------------------------------------------------------------

Result<ResolvedPackage> DependencyResolver::resolve_path(
    const Dependency& dep,
    const fs::path& manifest_dir)
{
    fs::path dep_path(dep.path->path);

    // Resolve relative paths against manifest directory
    if (!dep_path.is_absolute()) {
        dep_path = manifest_dir / dep_path;
    }

    // Canonicalize
    std::error_code ec;
    fs::path canonical = fs::canonical(dep_path, ec);
    if (ec) {
        return LoomError{LoomError::NotFound,
            "path dependency '" + dep.name + "': directory does not exist: " +
            dep_path.string()};
    }
    dep_path = canonical;

    // Check for Loom.toml
    fs::path manifest_file = dep_path / "Loom.toml";
    if (!fs::exists(manifest_file, ec)) {
        return LoomError{LoomError::Manifest,
            "path dependency '" + dep.name +
            "': no Loom.toml found in " + dep_path.string()};
    }

    // Load manifest to get name/version
    auto manifest = Manifest::load(manifest_file.string());
    if (manifest.is_err()) return std::move(manifest).error();

    // Compute checksum
    auto checksum = cache_.compute_checksum(dep_path.string());
    if (checksum.is_err()) return std::move(checksum).error();

    ResolvedPackage pkg;
    pkg.name = dep.name;
    pkg.version = manifest.value().package.version;
    pkg.source_url = dep_path.string();
    pkg.is_path = true;
    pkg.checksum = checksum.value();

    return Result<ResolvedPackage>::ok(std::move(pkg));
}

// ---------------------------------------------------------------------------
// load_transitive_deps()
// ---------------------------------------------------------------------------

Result<std::vector<Dependency>> DependencyResolver::load_transitive_deps(
    const ResolvedPackage& pkg)
{
    Manifest manifest;

    if (pkg.is_path) {
        fs::path manifest_path = fs::path(pkg.source_url) / "Loom.toml";
        auto m = Manifest::load(manifest_path.string());
        if (m.is_err()) return std::move(m).error();
        manifest = std::move(m).value();
    } else {
        // Git dep: read Loom.toml from the bare repo at the resolved commit
        auto bare = cache_.bare_repo_path(pkg.name, pkg.source_url);
        auto content = cache_.git().show_file(bare, pkg.commit, "Loom.toml");
        if (content.is_err()) {
            // No Loom.toml in the repo — no transitive deps
            if (content.error().code == LoomError::NotFound ||
                content.error().code == LoomError::IO)
            {
                return Result<std::vector<Dependency>>::ok({});
            }
            return std::move(content).error();
        }
        auto m = Manifest::parse(content.value());
        if (m.is_err()) return std::move(m).error();
        manifest = std::move(m).value();
    }

    // Filter out workspace/member deps from transitive deps — they're invalid
    // in non-root packages
    std::vector<Dependency> result;
    for (auto& dep : manifest.dependencies) {
        if (dep.workspace || dep.member) {
            log::warn("ignoring workspace/member dependency '%s' in "
                      "transitive dependency '%s'",
                      dep.name.c_str(), pkg.name.c_str());
            continue;
        }
        result.push_back(std::move(dep));
    }

    return Result<std::vector<Dependency>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// apply_overrides() — static
// ---------------------------------------------------------------------------

Status DependencyResolver::apply_overrides(LockFile& lockfile,
                                            const LocalOverrides& overrides)
{
    for (const auto& [name, src] : overrides.overrides) {
        // Find matching locked package
        LockedPackage* target = nullptr;
        for (auto& pkg : lockfile.packages) {
            if (pkg.name == name) {
                target = &pkg;
                break;
            }
        }

        if (!target) {
            log::warn("local override for '%s' has no matching locked package, "
                      "skipping", name.c_str());
            continue;
        }

        if (src.kind == OverrideSource::Kind::Path) {
            target->source = "path+" + src.path;
            target->commit.clear();
            target->ref.clear();
            log::info("override: %s -> path '%s'", name.c_str(),
                      src.path.c_str());
        } else {
            target->source = "git+" + src.url;
            if (!src.tag.empty()) target->ref = src.tag;
            else if (!src.branch.empty()) target->ref = src.branch;
            else if (!src.rev.empty()) target->ref = src.rev;
            log::info("override: %s -> git '%s'", name.c_str(),
                      src.url.c_str());
        }
    }

    return ok_status();
}

// ---------------------------------------------------------------------------
// topological_sort() — static
// ---------------------------------------------------------------------------

Result<std::vector<std::string>> DependencyResolver::topological_sort(
    const LockFile& lockfile)
{
    GraphMap<> graph;

    for (const auto& pkg : lockfile.packages) {
        graph.add_node(pkg.name);
        for (const auto& dep : pkg.dependencies) {
            graph.add_edge(pkg.name, dep);
        }
    }

    return graph.topological_sort();
}

// ---------------------------------------------------------------------------
// build_lockfile() — static
// ---------------------------------------------------------------------------

LockFile DependencyResolver::build_lockfile(
    const Manifest& root_manifest,
    const std::unordered_map<std::string, ResolvedPackage>& resolved)
{
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = root_manifest.package.name;
    lf.root_version = root_manifest.package.version;

    for (const auto& [name, pkg] : resolved) {
        LockedPackage lp;
        lp.name = pkg.name;
        lp.version = pkg.version;
        lp.source = pkg.is_path
            ? "path+" + pkg.source_url
            : "git+" + pkg.source_url;
        lp.commit = pkg.commit;
        lp.ref = pkg.ref;
        lp.checksum = pkg.checksum;
        lp.dependencies = pkg.dep_names;

        lf.packages.push_back(std::move(lp));
    }

    // Sort packages by name for deterministic output
    std::sort(lf.packages.begin(), lf.packages.end(),
              [](const LockedPackage& a, const LockedPackage& b) {
                  return a.name < b.name;
              });

    return lf;
}

} // namespace loom
