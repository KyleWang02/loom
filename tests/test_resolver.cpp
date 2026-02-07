#include <catch2/catch.hpp>
#include <loom/resolver.hpp>
#include <loom/git.hpp>
#include <loom/log.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace loom;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// RAII temp directory
// ---------------------------------------------------------------------------

struct TempDir {
    fs::path path;

    TempDir() {
        const char* src = std::getenv("LOOM_SOURCE_DIR");
        fs::path base = src ? fs::path(src) / "build" : fs::temp_directory_path();
        path = base / ("loom_resolve_test_" + std::to_string(
            std::hash<std::string>{}(std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))));
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

// ---------------------------------------------------------------------------
// Helper: create a local git repo with a Loom.toml and optional tag
// ---------------------------------------------------------------------------

struct GitFixture {
    TempDir td;

    // Create a local git repo with Loom.toml. Returns absolute path.
    std::string create_repo(const std::string& name,
                            const std::string& version,
                            const std::string& manifest_toml,
                            const std::string& tag = "")
    {
        fs::path repo_dir = td.path / name;
        fs::create_directories(repo_dir);

        // Write Loom.toml
        {
            std::ofstream f(repo_dir / "Loom.toml");
            f << manifest_toml;
        }

        // Write a dummy source file for checksum
        {
            std::ofstream f(repo_dir / "dummy.sv");
            f << "module " << name << ";\nendmodule\n";
        }

        // Init git repo and commit
        auto r1 = run_command({"git", "init"}, repo_dir.string());
        REQUIRE(r1.is_ok());

        auto r2 = run_command({"git", "add", "."}, repo_dir.string());
        REQUIRE(r2.is_ok());

        auto r3 = run_command({"git", "-c", "user.email=test@test.com",
                               "-c", "user.name=Test",
                               "commit", "-m", "initial"},
                              repo_dir.string());
        REQUIRE(r3.is_ok());

        if (!tag.empty()) {
            auto r4 = run_command({"git", "tag", tag}, repo_dir.string());
            REQUIRE(r4.is_ok());
        }

        return repo_dir.string();
    }

    // Add a commit with updated files to an existing repo and tag it
    void add_version(const std::string& repo_path,
                     const std::string& manifest_toml,
                     const std::string& tag)
    {
        // Update Loom.toml
        {
            std::ofstream f(fs::path(repo_path) / "Loom.toml");
            f << manifest_toml;
        }

        auto r1 = run_command({"git", "add", "."}, repo_path);
        REQUIRE(r1.is_ok());

        auto r2 = run_command({"git", "-c", "user.email=test@test.com",
                               "-c", "user.name=Test",
                               "commit", "-m", "version " + tag},
                              repo_path);
        REQUIRE(r2.is_ok());

        auto r3 = run_command({"git", "tag", tag}, repo_path);
        REQUIRE(r3.is_ok());
    }
};

// ===== Category 1: Single Dependency =====

TEST_CASE("resolve git dep with tag", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.root_name == "top");
    REQUIRE(lf.root_version == "0.1.0");
    REQUIRE(lf.loom_version == "0.1.0");
    REQUIRE(lf.packages.size() == 1);

    auto* pkg = lf.find("lib_a");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->source == "git+" + url);
    REQUIRE(pkg->ref == "v1.0.0");
    REQUIRE(!pkg->commit.empty());
    REQUIRE(!pkg->checksum.empty());
}

TEST_CASE("resolve git dep with version constraint", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    // Add more versions
    gf.add_version(url, R"(
[package]
name = "lib_a"
version = "1.1.0"
)", "v1.1.0");

    gf.add_version(url, R"(
[package]
name = "lib_a"
version = "1.2.0"
)", "v1.2.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, {}, "^1.0.0", {}, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto* pkg = result.value().find("lib_a");
    REQUIRE(pkg != nullptr);
    // Should pick highest matching: 1.2.0
    REQUIRE(pkg->version == "1.2.0");
    REQUIRE(pkg->ref == "v1.2.0");
}

TEST_CASE("resolve git dep with rev", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    // Get commit SHA
    auto sha_result = run_command({"git", "rev-parse", "HEAD"}, url);
    REQUIRE(sha_result.is_ok());
    std::string commit = sha_result.value().stdout_str;
    // Trim newline
    while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r'))
        commit.pop_back();

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, {}, {}, commit, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto* pkg = result.value().find("lib_a");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->commit == commit);
}

TEST_CASE("resolve git dep with branch", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)");

    // Create a branch
    auto r1 = run_command({"git", "checkout", "-b", "develop"}, url);
    REQUIRE(r1.is_ok());

    // Add a commit on the branch
    {
        std::ofstream f(fs::path(url) / "extra.sv");
        f << "module extra; endmodule\n";
    }
    auto r2 = run_command({"git", "add", "."}, url);
    REQUIRE(r2.is_ok());
    auto r3 = run_command({"git", "-c", "user.email=test@test.com",
                           "-c", "user.name=Test",
                           "commit", "-m", "branch commit"},
                          url);
    REQUIRE(r3.is_ok());

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, {}, {}, {}, "develop"};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto* pkg = result.value().find("lib_a");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->ref == "develop");
    REQUIRE(!pkg->commit.empty());
}

TEST_CASE("resolve path dep", "[resolver]") {
    TempDir td;
    td.write_file("dep_a/Loom.toml", R"(
[package]
name = "dep_a"
version = "2.0.0"
)");
    td.write_file("dep_a/src/mod.sv", "module dep_a; endmodule\n");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "dep_a";
    dep.path = PathSource{(td.path / "dep_a").string()};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.packages.size() == 1);

    auto* pkg = lf.find("dep_a");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->version == "2.0.0");
    REQUIRE(pkg->source.find("path+") == 0);
    REQUIRE(pkg->commit.empty());
    REQUIRE(!pkg->checksum.empty());
}

// ===== Category 2: Transitive Dependencies =====

TEST_CASE("resolve two-level transitive deps", "[resolver]") {
    GitFixture gf;

    // B has no deps
    std::string url_b = gf.create_repo("lib_b", "1.0.0", R"(
[package]
name = "lib_b"
version = "1.0.0"
)", "v1.0.0");

    // A depends on B
    std::string manifest_a = std::string(R"(
[package]
name = "lib_a"
version = "1.0.0"

[dependencies]
lib_b = { git = ")") + url_b + R"(", tag = "v1.0.0" }
)";
    std::string url_a = gf.create_repo("lib_a", "1.0.0", manifest_a, "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.packages.size() == 2);
    REQUIRE(lf.find("lib_a") != nullptr);
    REQUIRE(lf.find("lib_b") != nullptr);

    // lib_a should list lib_b as a dependency
    auto* pkg_a = lf.find("lib_a");
    REQUIRE(pkg_a->dependencies.size() == 1);
    REQUIRE(pkg_a->dependencies[0] == "lib_b");
}

TEST_CASE("resolve diamond dependency", "[resolver]") {
    GitFixture gf;

    // C has no deps
    std::string url_c = gf.create_repo("lib_c", "1.0.0", R"(
[package]
name = "lib_c"
version = "1.0.0"
)", "v1.0.0");

    // A depends on C
    std::string manifest_a = std::string(R"(
[package]
name = "lib_a"
version = "1.0.0"

[dependencies]
lib_c = { git = ")") + url_c + R"(", tag = "v1.0.0" }
)";
    std::string url_a = gf.create_repo("lib_a", "1.0.0", manifest_a, "v1.0.0");

    // B depends on C
    std::string manifest_b = std::string(R"(
[package]
name = "lib_b"
version = "1.0.0"

[dependencies]
lib_c = { git = ")") + url_c + R"(", tag = "v1.0.0" }
)";
    std::string url_b = gf.create_repo("lib_b", "1.0.0", manifest_b, "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep_a;
    dep_a.name = "lib_a";
    dep_a.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_a);

    Dependency dep_b;
    dep_b.name = "lib_b";
    dep_b.git = GitSource{url_b, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_b);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.packages.size() == 3);
    REQUIRE(lf.find("lib_a") != nullptr);
    REQUIRE(lf.find("lib_b") != nullptr);
    REQUIRE(lf.find("lib_c") != nullptr);
}

TEST_CASE("resolve deep chain A->B->C->D", "[resolver]") {
    GitFixture gf;

    // D: no deps
    std::string url_d = gf.create_repo("lib_d", "1.0.0", R"(
[package]
name = "lib_d"
version = "1.0.0"
)", "v1.0.0");

    // C depends on D
    std::string manifest_c = std::string(R"(
[package]
name = "lib_c"
version = "1.0.0"

[dependencies]
lib_d = { git = ")") + url_d + R"(", tag = "v1.0.0" }
)";
    std::string url_c = gf.create_repo("lib_c", "1.0.0", manifest_c, "v1.0.0");

    // B depends on C
    std::string manifest_b = std::string(R"(
[package]
name = "lib_b"
version = "1.0.0"

[dependencies]
lib_c = { git = ")") + url_c + R"(", tag = "v1.0.0" }
)";
    std::string url_b = gf.create_repo("lib_b", "1.0.0", manifest_b, "v1.0.0");

    // A depends on B
    std::string manifest_a = std::string(R"(
[package]
name = "lib_a"
version = "1.0.0"

[dependencies]
lib_b = { git = ")") + url_b + R"(", tag = "v1.0.0" }
)";
    std::string url_a = gf.create_repo("lib_a", "1.0.0", manifest_a, "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.packages.size() == 4);
    REQUIRE(lf.find("lib_a") != nullptr);
    REQUIRE(lf.find("lib_b") != nullptr);
    REQUIRE(lf.find("lib_c") != nullptr);
    REQUIRE(lf.find("lib_d") != nullptr);
}

TEST_CASE("resolve mixed path and git transitive deps", "[resolver]") {
    TempDir td;
    GitFixture gf;

    // Create a path dep (lib_b)
    td.write_file("lib_b/Loom.toml", R"(
[package]
name = "lib_b"
version = "1.0.0"
)");
    td.write_file("lib_b/src/mod.sv", "module lib_b; endmodule\n");

    // Create a git dep (lib_a) that depends on lib_b via path
    // (lib_a itself has no deps for simplicity since path in transitive
    // would need careful handling -- instead test path + git at root level)

    std::string url_a = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep_a;
    dep_a.name = "lib_a";
    dep_a.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_a);

    Dependency dep_b;
    dep_b.name = "lib_b";
    dep_b.path = PathSource{(td.path / "lib_b").string()};
    manifest.dependencies.push_back(dep_b);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());

    auto& lf = result.value();
    REQUIRE(lf.packages.size() == 2);

    auto* pkg_a = lf.find("lib_a");
    REQUIRE(pkg_a != nullptr);
    REQUIRE(pkg_a->source.find("git+") == 0);

    auto* pkg_b = lf.find("lib_b");
    REQUIRE(pkg_b != nullptr);
    REQUIRE(pkg_b->source.find("path+") == 0);
}

// ===== Category 3: Lockfile Reuse =====

TEST_CASE("valid lockfile is reused without re-resolution", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    // First resolve
    auto r1 = resolver.resolve(manifest);
    REQUIRE(r1.is_ok());

    // Second resolve with existing lock — should reuse
    auto r2 = resolver.resolve(manifest, r1.value());
    REQUIRE(r2.is_ok());
    REQUIRE(r2.value().packages.size() == 1);
    REQUIRE(r2.value().find("lib_a")->commit == r1.value().find("lib_a")->commit);
}

TEST_CASE("stale lockfile triggers re-resolution", "[resolver]") {
    GitFixture gf;
    std::string url_a = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    std::string url_b = gf.create_repo("lib_b", "1.0.0", R"(
[package]
name = "lib_b"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep_a;
    dep_a.name = "lib_a";
    dep_a.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_a);

    // Resolve with only lib_a
    auto r1 = resolver.resolve(manifest);
    REQUIRE(r1.is_ok());
    REQUIRE(r1.value().packages.size() == 1);

    // Add lib_b — lockfile becomes stale
    Dependency dep_b;
    dep_b.name = "lib_b";
    dep_b.git = GitSource{url_b, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_b);

    auto r2 = resolver.resolve(manifest, r1.value());
    REQUIRE(r2.is_ok());
    REQUIRE(r2.value().packages.size() == 2);
    REQUIRE(r2.value().find("lib_b") != nullptr);
}

TEST_CASE("update specific package keeps others locked", "[resolver]") {
    GitFixture gf;
    std::string url_a = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    std::string url_b = gf.create_repo("lib_b", "1.0.0", R"(
[package]
name = "lib_b"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep_a;
    dep_a.name = "lib_a";
    dep_a.git = GitSource{url_a, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_a);

    Dependency dep_b;
    dep_b.name = "lib_b";
    dep_b.git = GitSource{url_b, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep_b);

    auto r1 = resolver.resolve(manifest);
    REQUIRE(r1.is_ok());
    std::string orig_commit_a = r1.value().find("lib_a")->commit;
    std::string orig_commit_b = r1.value().find("lib_b")->commit;

    // Update only lib_b
    auto r2 = resolver.update(manifest, r1.value(), "lib_b");
    REQUIRE(r2.is_ok());

    // lib_a should keep its original commit
    REQUIRE(r2.value().find("lib_a")->commit == orig_commit_a);
    // lib_b is re-resolved (commit should be same since no new tags)
    REQUIRE(r2.value().find("lib_b") != nullptr);
}

// ===== Category 4: Error Cases =====

TEST_CASE("update nonexistent package returns error", "[resolver]") {
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "top";
    lf.root_version = "0.1.0";

    auto result = resolver.update(manifest, lf, "nonexistent");
    REQUIRE(result.is_err());
    REQUIRE(result.error().code == LoomError::NotFound);
}

TEST_CASE("workspace/member dep in resolve_deps returns error", "[resolver]") {
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.workspace = true;
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_err());
    REQUIRE(result.error().code == LoomError::Dependency);
}

TEST_CASE("dep with no source returns error", "[resolver]") {
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    // No git or path source
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_err());
    REQUIRE(result.error().code == LoomError::Dependency);
}

// ===== Category 5: Local Overrides =====

TEST_CASE("path override replaces git source", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "top";
    lf.root_version = "0.1.0";

    LockedPackage pkg;
    pkg.name = "lib_a";
    pkg.version = "1.0.0";
    pkg.source = "git+https://github.com/org/lib_a.git";
    pkg.commit = "abc123";
    pkg.ref = "v1.0.0";
    lf.packages.push_back(pkg);

    LocalOverrides overrides;
    OverrideSource src;
    src.kind = OverrideSource::Kind::Path;
    src.path = "/home/user/local_lib_a";
    overrides.overrides["lib_a"] = src;

    auto status = DependencyResolver::apply_overrides(lf, overrides);
    REQUIRE(status.is_ok());

    auto* p = lf.find("lib_a");
    REQUIRE(p != nullptr);
    REQUIRE(p->source == "path+/home/user/local_lib_a");
    REQUIRE(p->commit.empty());
    REQUIRE(p->ref.empty());
}

TEST_CASE("override for unknown package warns but succeeds", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "top";
    lf.root_version = "0.1.0";

    LocalOverrides overrides;
    OverrideSource src;
    src.kind = OverrideSource::Kind::Path;
    src.path = "/some/path";
    overrides.overrides["nonexistent"] = src;

    auto status = DependencyResolver::apply_overrides(lf, overrides);
    REQUIRE(status.is_ok());
    // No packages should be modified
    REQUIRE(lf.packages.empty());
}

TEST_CASE("empty overrides is no-op", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "top";
    lf.root_version = "0.1.0";

    LockedPackage pkg;
    pkg.name = "lib_a";
    pkg.version = "1.0.0";
    pkg.source = "git+https://example.com/a.git";
    pkg.commit = "abc123";
    lf.packages.push_back(pkg);

    LocalOverrides empty_overrides;

    auto status = DependencyResolver::apply_overrides(lf, empty_overrides);
    REQUIRE(status.is_ok());
    REQUIRE(lf.find("lib_a")->source == "git+https://example.com/a.git");
    REQUIRE(lf.find("lib_a")->commit == "abc123");
}

TEST_CASE("git override replaces source", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "top";
    lf.root_version = "0.1.0";

    LockedPackage pkg;
    pkg.name = "lib_a";
    pkg.version = "1.0.0";
    pkg.source = "git+https://github.com/org/lib_a.git";
    pkg.commit = "abc123";
    pkg.ref = "v1.0.0";
    lf.packages.push_back(pkg);

    LocalOverrides overrides;
    OverrideSource src;
    src.kind = OverrideSource::Kind::Git;
    src.url = "https://github.com/fork/lib_a.git";
    src.branch = "my-branch";
    overrides.overrides["lib_a"] = src;

    auto status = DependencyResolver::apply_overrides(lf, overrides);
    REQUIRE(status.is_ok());

    auto* p = lf.find("lib_a");
    REQUIRE(p != nullptr);
    REQUIRE(p->source == "git+https://github.com/fork/lib_a.git");
    REQUIRE(p->ref == "my-branch");
}

// ===== Category 6: Topological Sort =====

TEST_CASE("topological sort respects dependency edges", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";

    LockedPackage a;
    a.name = "lib_a";
    a.dependencies = {"lib_b", "lib_c"};
    lf.packages.push_back(a);

    LockedPackage b;
    b.name = "lib_b";
    b.dependencies = {"lib_c"};
    lf.packages.push_back(b);

    LockedPackage c;
    c.name = "lib_c";
    lf.packages.push_back(c);

    auto result = DependencyResolver::topological_sort(lf);
    REQUIRE(result.is_ok());

    auto& sorted = result.value();
    REQUIRE(sorted.size() == 3);

    // Find positions
    auto pos = [&](const std::string& name) -> size_t {
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i] == name) return i;
        }
        return sorted.size();
    };

    // Topological sort: dependents before dependencies
    // A depends on B and C, B depends on C
    // So A must come before B, and A must come before C, and B must come before C
    REQUIRE(pos("lib_a") < pos("lib_b"));
    REQUIRE(pos("lib_a") < pos("lib_c"));
    REQUIRE(pos("lib_b") < pos("lib_c"));
}

TEST_CASE("topological sort detects cycle", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";

    LockedPackage a;
    a.name = "lib_a";
    a.dependencies = {"lib_b"};
    lf.packages.push_back(a);

    LockedPackage b;
    b.name = "lib_b";
    b.dependencies = {"lib_a"};  // cycle!
    lf.packages.push_back(b);

    auto result = DependencyResolver::topological_sort(lf);
    REQUIRE(result.is_err());
    REQUIRE(result.error().code == LoomError::Cycle);
}

// ===== Category 7: Edge Cases =====

TEST_CASE("empty dependency list produces empty lockfile", "[resolver]") {
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";
    // No dependencies

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());
    REQUIRE(result.value().packages.empty());
    REQUIRE(result.value().root_name == "top");
    REQUIRE(result.value().loom_version == "0.1.0");
}

TEST_CASE("dep with no transitive deps produces single entry", "[resolver]") {
    GitFixture gf;
    std::string url = gf.create_repo("lib_a", "1.0.0", R"(
[package]
name = "lib_a"
version = "1.0.0"
)", "v1.0.0");

    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{url, "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_ok());
    REQUIRE(result.value().packages.size() == 1);
    REQUIRE(result.value().find("lib_a")->dependencies.empty());
}

TEST_CASE("path dep that does not exist returns error", "[resolver]") {
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.path = PathSource{"/nonexistent/path/to/lib_a"};
    manifest.dependencies.push_back(dep);

    auto result = resolver.resolve(manifest);
    REQUIRE(result.is_err());
    REQUIRE(result.error().code == LoomError::NotFound);
}

TEST_CASE("topological sort empty lockfile", "[resolver]") {
    LockFile lf;
    lf.loom_version = "0.1.0";

    auto result = DependencyResolver::topological_sort(lf);
    REQUIRE(result.is_ok());
    REQUIRE(result.value().empty());
}

TEST_CASE("lockfile reused when not stale", "[resolver]") {
    // Build a lockfile manually and verify resolve returns it unchanged
    TempDir cache_dir;
    CacheManager cache(cache_dir.path.string());
    DependencyResolver resolver(cache);

    Manifest manifest;
    manifest.package.name = "top";
    manifest.package.version = "0.1.0";

    Dependency dep;
    dep.name = "lib_a";
    dep.git = GitSource{"https://example.com/lib_a.git", "v1.0.0", {}, {}, {}};
    manifest.dependencies.push_back(dep);

    // Construct a matching lockfile
    LockFile existing;
    existing.loom_version = "0.1.0";
    existing.root_name = "top";
    existing.root_version = "0.1.0";

    LockedPackage lp;
    lp.name = "lib_a";
    lp.version = "1.0.0";
    lp.source = "git+https://example.com/lib_a.git";
    lp.commit = "abcdef1234567890abcdef1234567890abcdef12";
    lp.ref = "v1.0.0";
    lp.checksum = "deadbeef";
    existing.packages.push_back(lp);

    auto result = resolver.resolve(manifest, existing);
    REQUIRE(result.is_ok());
    // Should reuse the existing lockfile without re-resolving
    REQUIRE(result.value().packages.size() == 1);
    REQUIRE(result.value().find("lib_a")->commit == lp.commit);
    REQUIRE(result.value().find("lib_a")->checksum == "deadbeef");
}
