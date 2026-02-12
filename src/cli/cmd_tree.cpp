#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/resolver.hpp>
#include <loom/lockfile.hpp>
#include <loom/local_override.hpp>
#include <loom/graph.hpp>
#include <iostream>
#include <filesystem>
#include <unordered_set>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_tree(CliArgs& global, CliArgs& /*cmd*/) {
    auto cwd = fs::current_path();

    // Discover project
    auto proj_r = Project::discover(cwd);
    if (proj_r.is_err()) return std::move(proj_r).error();
    auto& project = proj_r.value();

    // Load lockfile
    auto lock_path = project.root_dir / "Loom.lock";
    if (!fs::exists(lock_path)) {
        return LoomError(LoomError::NotFound,
            "no Loom.lock found -- run 'loom lock' first",
            "loom lock");
    }

    auto lock_r = LockFile::load(lock_path.string());
    if (lock_r.is_err()) return std::move(lock_r).error();
    auto& lockfile = lock_r.value();

    if (lockfile.packages.empty()) {
        std::cout << project.manifest.package.name << " v"
                  << project.manifest.package.version << "\n";
        std::cout << "(no dependencies)\n";
        return Result<int>::ok(0);
    }

    // Topological sort to verify graph is valid
    auto sort_r = DependencyResolver::topological_sort(lockfile);
    if (sort_r.is_err()) return std::move(sort_r).error();

    // Discover local overrides to annotate tree (if active)
    std::unordered_set<std::string> overridden_packages;
    bool suppress = should_suppress_overrides(global.has("no-local"));
    if (!suppress) {
        auto local_r = discover_local_overrides(project.root_dir);
        if (local_r.is_ok() && !local_r.value().empty()) {
            local_r.value().warn_active();
            for (const auto& [name, _] : local_r.value().overrides) {
                overridden_packages.insert(name);
            }
        }
    }

    // Build GraphMap from lockfile
    std::string root_name = project.manifest.package.name + " v"
                            + project.manifest.package.version;
    GraphMap<> graph;
    graph.add_node(root_name);

    // Map package names to display strings
    std::unordered_map<std::string, std::string> display_names;
    for (const auto& pkg : lockfile.packages) {
        std::string display = pkg.name + " v" + pkg.version;
        if (overridden_packages.count(pkg.name)) {
            display += " (override)";
        }
        display_names[pkg.name] = display;
        graph.add_node(display);
    }

    // Add edges: root -> direct deps (from manifest)
    for (const auto& dep : project.manifest.dependencies) {
        auto it = display_names.find(dep.name);
        if (it != display_names.end()) {
            graph.add_edge(root_name, it->second);
        }
    }

    // Add edges: package -> its dependencies (from lockfile)
    for (const auto& pkg : lockfile.packages) {
        const std::string& from = display_names[pkg.name];
        for (const auto& dep_name : pkg.dependencies) {
            auto it = display_names.find(dep_name);
            if (it != display_names.end()) {
                graph.add_edge(from, it->second);
            }
        }
    }

    // Print tree
    std::cout << graph.tree_display(root_name);

    return Result<int>::ok(0);
}

void register_tree(CliParser& cli) {
    Command cmd;
    cmd.name = "tree";
    cmd.summary = "Display the dependency tree";
    cmd.description = "Loads Loom.lock and displays the full dependency tree as an ASCII "
                      "diagram. Packages with active Loom.local overrides are annotated. "
                      "Cyclic or repeated subtrees are marked with (*).";
    cmd.usage = "loom tree";
    cmd.group = "Dependencies";
    cmd.flags = {};
    cmd.handler = handle_tree;
    cli.add_command(std::move(cmd));
}

} // namespace loom
