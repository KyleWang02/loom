// demo_resolver.cpp — Dependency Resolution demonstration
//
// Exercises DependencyResolver with real local git repos:
//   1. Single dependency resolution (git + tag)
//   2. Version constraint resolution (picks highest match)
//   3. Transitive dependency chain (A → B → C)
//   4. Lockfile generation, save to disk, reload
//   5. Topological sort of resolved packages
//   6. Lockfile staleness detection
//
// All operations use temporary directories (auto-cleaned).

#include <loom/resolver.hpp>
#include <loom/lockfile.hpp>
#include <loom/cache.hpp>
#include <loom/git.hpp>
#include <loom/log.hpp>
#include <loom/manifest.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace loom;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void print_header(const char* title) {
    std::cout << "\n"
              << "========================================================\n"
              << "  " << title << "\n"
              << "========================================================\n\n";
}

struct TempDir {
    fs::path path;
    TempDir(const std::string& suffix = "main") {
        path = fs::temp_directory_path() / ("loom_demo_resolve_" + suffix + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write_file(const std::string& rel, const std::string& content) {
        fs::path full = path / rel;
        fs::create_directories(full.parent_path());
        std::ofstream f(full);
        f << content;
    }
};

// Create a local git repo with Loom.toml and optional source file.
// Returns the absolute path to the repo.
static std::string create_repo(TempDir& td,
                                const std::string& name,
                                const std::string& manifest_toml,
                                const std::string& tag = "",
                                const std::string& sv_source = "") {
    fs::path repo_dir = td.path / name;
    fs::create_directories(repo_dir);

    // Write Loom.toml
    {
        std::ofstream f(repo_dir / "Loom.toml");
        f << manifest_toml;
    }

    // Write a source file
    std::string src = sv_source.empty()
        ? ("module " + name + ";\nendmodule\n")
        : sv_source;
    {
        std::ofstream f(repo_dir / (name + ".sv"));
        f << src;
    }

    // Init git repo
    auto r1 = run_command({"git", "init"}, repo_dir.string());
    if (r1.is_err()) { std::cerr << "git init failed\n"; std::exit(1); }

    auto r2 = run_command({"git", "add", "."}, repo_dir.string());
    if (r2.is_err()) { std::cerr << "git add failed\n"; std::exit(1); }

    auto r3 = run_command({"git", "-c", "user.email=demo@loom.dev",
                           "-c", "user.name=Loom Demo",
                           "commit", "-m", "initial"},
                          repo_dir.string());
    if (r3.is_err()) { std::cerr << "git commit failed\n"; std::exit(1); }

    if (!tag.empty()) {
        auto r4 = run_command({"git", "tag", tag}, repo_dir.string());
        if (r4.is_err()) { std::cerr << "git tag failed\n"; std::exit(1); }
    }

    return repo_dir.string();
}

// Add a new version (new commit + tag) to an existing repo
static void add_version(const std::string& repo_path,
                         const std::string& manifest_toml,
                         const std::string& tag) {
    {
        std::ofstream f(fs::path(repo_path) / "Loom.toml");
        f << manifest_toml;
    }
    run_command({"git", "add", "."}, repo_path);
    run_command({"git", "-c", "user.email=demo@loom.dev",
                 "-c", "user.name=Loom Demo",
                 "commit", "-m", "version " + tag},
                repo_path);
    run_command({"git", "tag", tag}, repo_path);
}

static void print_lockfile(const LockFile& lf) {
    std::cout << "  Root: " << lf.root_name << " v" << lf.root_version << "\n";
    std::cout << "  Loom version: " << lf.loom_version << "\n";
    std::cout << "  Packages (" << lf.packages.size() << "):\n\n";
    for (auto& pkg : lf.packages) {
        std::cout << "    " << pkg.name << " v" << pkg.version << "\n";
        std::cout << "      source:   " << pkg.source << "\n";
        std::cout << "      ref:      " << pkg.ref << "\n";
        std::cout << "      commit:   " << pkg.commit.substr(0, 12) << "...\n";
        std::cout << "      checksum: " << pkg.checksum.substr(0, 16) << "...\n";
        if (!pkg.dependencies.empty()) {
            std::cout << "      deps:     [";
            for (size_t i = 0; i < pkg.dependencies.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << pkg.dependencies[i];
            }
            std::cout << "]\n";
        }
        std::cout << "\n";
    }
}

// ===========================================================================
// Demo scenarios
// ===========================================================================

static void demo_single_dep() {
    print_header("1. SINGLE DEPENDENCY (git + tag)");

    TempDir repos("repos1");
    TempDir cache_dir("cache1");

    // Create a library repo
    std::string lib_url = create_repo(repos, "uart_lib",
        R"([package]
name = "uart_lib"
version = "1.0.0"
)", "v1.0.0",
        "module uart_tx(input logic clk, output logic tx);\nendmodule\n");

    std::cout << "Library repo: " << lib_url << "\n";
    std::cout << "Tag: v1.0.0\n\n";

    // Create a manifest depending on it
    Manifest manifest;
    manifest.package.name = "my_project";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "uart_lib";
    dep.git = GitSource{lib_url, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    // Resolve
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    std::cout << "Resolving dependencies for my_project v0.1.0...\n\n";
    auto result = resolver.resolve(manifest);

    if (result.is_err()) {
        std::cerr << "ERROR: " << result.error().format() << "\n";
        return;
    }

    print_lockfile(result.value());
    std::cout << "  Result: SUCCESS - uart_lib resolved at v1.0.0\n";
}

static void demo_version_constraint() {
    print_header("2. VERSION CONSTRAINT (picks highest matching)");

    TempDir repos("repos2");
    TempDir cache_dir("cache2");

    // Create a library with multiple versions
    std::string lib_url = create_repo(repos, "math_lib",
        R"([package]
name = "math_lib"
version = "1.0.0"
)", "v1.0.0");

    add_version(lib_url,
        R"([package]
name = "math_lib"
version = "1.1.0"
)", "v1.1.0");

    add_version(lib_url,
        R"([package]
name = "math_lib"
version = "1.2.0"
)", "v1.2.0");

    add_version(lib_url,
        R"([package]
name = "math_lib"
version = "2.0.0"
)", "v2.0.0");

    std::cout << "Library repo: math_lib with tags v1.0.0, v1.1.0, v1.2.0, v2.0.0\n";
    std::cout << "Constraint: ^1.0.0 (compatible with 1.x.x)\n\n";

    Manifest manifest;
    manifest.package.name = "my_project";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "math_lib";
    dep.git = GitSource{lib_url, {}, "^1.0.0", {}, {}};
    manifest.dependencies.push_back(dep);

    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    std::cout << "Resolving...\n\n";
    auto result = resolver.resolve(manifest);

    if (result.is_err()) {
        std::cerr << "ERROR: " << result.error().format() << "\n";
        return;
    }

    auto* pkg = result.value().find("math_lib");
    if (pkg) {
        std::cout << "  Resolved: math_lib v" << pkg->version
                  << " (ref: " << pkg->ref << ")\n";
        std::cout << "  Expected: v1.2.0 (highest ^1.0.0-compatible)\n";
        std::cout << "  Match: " << (pkg->version == "1.2.0" ? "YES" : "NO") << "\n";
    }
}

static void demo_transitive_deps() {
    print_header("3. TRANSITIVE DEPENDENCY CHAIN (A -> B -> C)");

    TempDir repos("repos3");
    TempDir cache_dir("cache3");

    // Create C (leaf, no deps)
    std::string url_c = create_repo(repos, "lib_c",
        R"([package]
name = "lib_c"
version = "1.0.0"
)", "v1.0.0",
        "module fifo(input logic clk); endmodule\n");

    // Create B (depends on C)
    std::string url_b = create_repo(repos, "lib_b",
        std::string(R"([package]
name = "lib_b"
version = "1.0.0"

[dependencies]
lib_c = { git = ")") + url_c + R"(", tag = "v1.0.0" }
)", "v1.0.0",
        "module uart_rx(input logic clk); fifo f1(); endmodule\n");

    // Create A (depends on B)
    std::string url_a = create_repo(repos, "lib_a",
        std::string(R"([package]
name = "lib_a"
version = "1.0.0"

[dependencies]
lib_b = { git = ")") + url_b + R"(", tag = "v1.0.0" }
)", "v1.0.0",
        "module top; uart_rx u1(); endmodule\n");

    std::cout << "Dependency chain:\n";
    std::cout << "  my_project -> lib_a -> lib_b -> lib_c\n\n";

    Manifest manifest;
    manifest.package.name = "my_project";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    std::cout << "Resolving (BFS traversal)...\n\n";
    auto result = resolver.resolve(manifest);

    if (result.is_err()) {
        std::cerr << "ERROR: " << result.error().format() << "\n";
        return;
    }

    auto& lf = result.value();
    print_lockfile(lf);
    std::cout << "  All 3 transitive deps resolved: "
              << (lf.packages.size() == 3 ? "YES" : "NO") << "\n";
}

static void demo_lockfile_persistence() {
    print_header("4. LOCKFILE PERSISTENCE (save + reload)");

    TempDir repos("repos4");
    TempDir cache_dir("cache4");

    std::string url = create_repo(repos, "persist_lib",
        R"([package]
name = "persist_lib"
version = "2.0.0"
)", "v2.0.0");

    Manifest manifest;
    manifest.package.name = "persist_project";
    manifest.package.version = "1.0.0";

    Dependency dep;
    dep.name = "persist_lib";
    dep.git = GitSource{url, "v2.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);
    auto result = resolver.resolve(manifest);

    if (result.is_err()) {
        std::cerr << "ERROR: " << result.error().format() << "\n";
        return;
    }

    // Save lockfile
    auto lock_path = (repos.path / "Loom.lock").string();
    auto save_status = result.value().save(lock_path);
    std::cout << "Saved Loom.lock to: " << lock_path << "\n";
    std::cout << "  Save status: " << (save_status.is_ok() ? "OK" : "FAIL") << "\n\n";

    // Read the file content
    std::ifstream lock_file(lock_path);
    std::string content((std::istreambuf_iterator<char>(lock_file)),
                         std::istreambuf_iterator<char>());
    std::cout << "Loom.lock contents:\n";
    std::cout << "  ----------------------------------------\n";
    for (auto& line : [&]() {
        std::vector<std::string> lines;
        std::istringstream iss(content);
        std::string l;
        while (std::getline(iss, l)) lines.push_back(l);
        return lines;
    }()) {
        std::cout << "  " << line << "\n";
    }
    std::cout << "  ----------------------------------------\n\n";

    // Reload
    auto reload = LockFile::load(lock_path);
    if (reload.is_err()) {
        std::cerr << "Reload ERROR: " << reload.error().format() << "\n";
        return;
    }
    auto& lf2 = reload.value();
    std::cout << "Reloaded lockfile:\n";
    std::cout << "  Root: " << lf2.root_name << " v" << lf2.root_version << "\n";
    std::cout << "  Packages: " << lf2.packages.size() << "\n";
    if (!lf2.packages.empty()) {
        std::cout << "  First package: " << lf2.packages[0].name
                  << " v" << lf2.packages[0].version << "\n";
    }
    std::cout << "  Roundtrip: SUCCESS\n";
}

static void demo_topological_sort() {
    print_header("5. TOPOLOGICAL SORT OF RESOLVED PACKAGES");

    TempDir repos("repos5");
    TempDir cache_dir("cache5");

    // Create a diamond: top -> {A, B} -> base
    std::string url_base = create_repo(repos, "base_lib",
        R"([package]
name = "base_lib"
version = "1.0.0"
)", "v1.0.0");

    std::string url_a = create_repo(repos, "lib_a",
        std::string(R"([package]
name = "lib_a"
version = "1.0.0"

[dependencies]
base_lib = { git = ")") + url_base + R"(", tag = "v1.0.0" }
)", "v1.0.0");

    std::string url_b = create_repo(repos, "lib_b",
        std::string(R"([package]
name = "lib_b"
version = "1.0.0"

[dependencies]
base_lib = { git = ")") + url_base + R"(", tag = "v1.0.0" }
)", "v1.0.0");

    Manifest manifest;
    manifest.package.name = "top_project";
    manifest.package.version = "0.1.0";

    Dependency dep_a, dep_b;
    dep_a.name = "lib_a";
    dep_a.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    dep_b.name = "lib_b";
    dep_b.git = GitSource{url_b, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_a);
    manifest.dependencies.push_back(dep_b);

    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    std::cout << "Diamond dependency graph:\n";
    std::cout << "  top_project -> lib_a -> base_lib\n";
    std::cout << "  top_project -> lib_b -> base_lib\n\n";

    auto result = resolver.resolve(manifest);
    if (result.is_err()) {
        std::cerr << "ERROR: " << result.error().format() << "\n";
        return;
    }

    std::cout << "Resolved " << result.value().packages.size() << " packages\n";

    // Topological sort
    auto topo = DependencyResolver::topological_sort(result.value());
    if (topo.is_err()) {
        std::cerr << "Topo sort ERROR: " << topo.error().format() << "\n";
        return;
    }

    std::cout << "\nTopological order (dependents first):\n";
    int i = 1;
    for (auto& name : topo.value()) {
        std::cout << "  " << i++ << ". " << name << "\n";
    }
    std::cout << "\n  base_lib should come LAST (it's a provider, depended upon by others)\n";
}

static void demo_staleness_check() {
    print_header("6. LOCKFILE STALENESS DETECTION");

    // Build a lockfile manually
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "my_project";
    lf.root_version = "1.0.0";

    LockedPackage pkg;
    pkg.name = "old_lib";
    pkg.version = "1.0.0";
    pkg.source = "git+https://example.com/old_lib.git";
    pkg.commit = "abc123";
    pkg.ref = "v1.0.0";
    lf.packages.push_back(pkg);

    // Case 1: manifest matches lockfile
    std::vector<Dependency> deps_match;
    {
        Dependency d;
        d.name = "old_lib";
        d.git = GitSource{"https://example.com/old_lib.git", "v1.0.0", {}, {}, {}};
        deps_match.push_back(d);
    }
    std::cout << "Case 1: Manifest has [old_lib] matching lockfile\n";
    std::cout << "  is_stale: " << (lf.is_stale(deps_match) ? "YES" : "NO") << "\n\n";

    // Case 2: manifest added a new dep
    std::vector<Dependency> deps_new = deps_match;
    {
        Dependency d;
        d.name = "new_lib";
        d.git = GitSource{"https://example.com/new_lib.git", "v1.0.0", {}, {}, {}};
        deps_new.push_back(d);
    }
    std::cout << "Case 2: Manifest added [new_lib] not in lockfile\n";
    std::cout << "  is_stale: " << (lf.is_stale(deps_new) ? "YES" : "NO") << "\n\n";

    // Case 3: manifest removed a dep
    std::vector<Dependency> deps_removed;
    std::cout << "Case 3: Manifest has no dependencies (removed old_lib)\n";
    std::cout << "  is_stale: " << (lf.is_stale(deps_removed) ? "YES" : "NO") << "\n";
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
    log::set_level(log::Warn);  // suppress info noise

    std::cout << "============================================================\n";
    std::cout << "     Loom Dependency Resolver — Comprehensive Demo\n";
    std::cout << "     Real git repos, BFS resolution, lockfile I/O\n";
    std::cout << "============================================================\n";

    demo_single_dep();
    demo_version_constraint();
    demo_transitive_deps();
    demo_lockfile_persistence();
    demo_topological_sort();
    demo_staleness_check();

    std::cout << "\n============================================================\n";
    std::cout << "     Demo complete. All resolver features exercised.\n";
    std::cout << "============================================================\n\n";

    return 0;
}
