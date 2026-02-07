#include <catch2/catch.hpp>
#include <loom/project.hpp>
#include <filesystem>
#include <fstream>

using namespace loom;
namespace fs = std::filesystem;

// RAII temp directory
struct TempDir {
    fs::path path;

    TempDir() {
        const char* src = std::getenv("LOOM_SOURCE_DIR");
        fs::path base = src ? fs::path(src) / "build" : fs::temp_directory_path();
        path = base / ("loom_test_" + std::to_string(
            std::hash<std::string>{}(std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    // Write a file relative to this dir
    void write_file(const std::string& rel, const std::string& content) {
        fs::path full = path / rel;
        fs::create_directories(full.parent_path());
        std::ofstream f(full);
        f << content;
    }
};

static const char* MINIMAL_MANIFEST = R"(
[package]
name = "test-pkg"
version = "0.1.0"
)";

// ===== find_manifest =====

TEST_CASE("find_manifest in current dir", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);

    auto r = find_manifest(td.path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().filename() == "Loom.toml");
    REQUIRE(r.value().parent_path() == fs::canonical(td.path));
}

TEST_CASE("find_manifest walks up", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);
    fs::create_directories(td.path / "a" / "b" / "c");

    auto r = find_manifest(td.path / "a" / "b" / "c");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().parent_path() == fs::canonical(td.path));
}

TEST_CASE("find_manifest returns NotFound", "[project]") {
    TempDir td;
    // No Loom.toml created

    auto r = find_manifest(td.path);
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::NotFound);
}

// ===== has_manifest =====

TEST_CASE("has_manifest true when present", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);
    REQUIRE(has_manifest(td.path));
}

TEST_CASE("has_manifest false when absent", "[project]") {
    TempDir td;
    REQUIRE_FALSE(has_manifest(td.path));
}

// ===== is_workspace_root =====

TEST_CASE("is_workspace_root true", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", R"(
[workspace]
members = ["ip/*"]
)");

    auto r = is_workspace_root(td.path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == true);
}

TEST_CASE("is_workspace_root false for regular project", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);

    auto r = is_workspace_root(td.path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == false);
}

TEST_CASE("is_workspace_root error when no manifest", "[project]") {
    TempDir td;
    auto r = is_workspace_root(td.path);
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::NotFound);
}

// ===== Project::load =====

TEST_CASE("Project::load parses manifest and computes checksum", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);

    auto r = Project::load(td.path);
    REQUIRE(r.is_ok());
    auto& proj = r.value();
    REQUIRE(proj.manifest.package.name == "test-pkg");
    REQUIRE(proj.manifest.package.version == "0.1.0");
    REQUIRE_FALSE(proj.checksum.empty());
    REQUIRE(proj.checksum.size() == 64);  // SHA-256 hex
    REQUIRE(proj.root_dir == fs::canonical(td.path));
    REQUIRE(proj.manifest_path == fs::canonical(td.path) / "Loom.toml");
}

TEST_CASE("Project::load fails for missing dir", "[project]") {
    auto r = Project::load("/nonexistent/path/to/project");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::IO);
}

TEST_CASE("Project::load consistent checksum", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);

    auto r1 = Project::load(td.path);
    auto r2 = Project::load(td.path);
    REQUIRE(r1.is_ok());
    REQUIRE(r2.is_ok());
    REQUIRE(r1.value().checksum == r2.value().checksum);
}

// ===== Project::discover =====

TEST_CASE("Project::discover from nested subdir", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", MINIMAL_MANIFEST);
    fs::create_directories(td.path / "src" / "deep");

    auto r = Project::discover(td.path / "src" / "deep");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().manifest.package.name == "test-pkg");
    REQUIRE(r.value().root_dir == fs::canonical(td.path));
}

// ===== collect_sources =====

TEST_CASE("collect_sources with no filtering", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", R"(
[package]
name = "test"
version = "0.1.0"

[[sources]]
files = ["rtl/top.sv", "rtl/bus.sv"]
)");
    td.write_file("rtl/top.sv", "module top; endmodule");
    td.write_file("rtl/bus.sv", "module bus; endmodule");

    auto r = Project::load(td.path);
    REQUIRE(r.is_ok());

    TargetSet empty;
    auto sources = r.value().collect_sources(empty);
    REQUIRE(sources.is_ok());
    REQUIRE(sources.value().size() == 2);
    // Paths should be absolute
    for (const auto& p : sources.value()) {
        REQUIRE(fs::path(p).is_absolute());
    }
}

TEST_CASE("collect_sources with target filtering", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", R"(
[package]
name = "test"
version = "0.1.0"

[[sources]]
files = ["rtl/top.sv"]

[[sources]]
target = "simulation"
files = ["tb/tb.sv"]

[[sources]]
target = "synth"
files = ["impl/wrapper.sv"]
)");
    td.write_file("rtl/top.sv", "");
    td.write_file("tb/tb.sv", "");
    td.write_file("impl/wrapper.sv", "");

    auto r = Project::load(td.path);
    REQUIRE(r.is_ok());

    TargetSet active = {"simulation"};
    auto sources = r.value().collect_sources(active);
    REQUIRE(sources.is_ok());
    // Should include rtl/top.sv (no target) + tb/tb.sv (simulation), not impl/wrapper.sv
    REQUIRE(sources.value().size() == 2);
}

TEST_CASE("collect_sources deduplicates", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", R"(
[package]
name = "test"
version = "0.1.0"

[[sources]]
files = ["rtl/top.sv"]

[[sources]]
files = ["rtl/top.sv"]
)");
    td.write_file("rtl/top.sv", "");

    auto r = Project::load(td.path);
    REQUIRE(r.is_ok());

    TargetSet empty;
    auto sources = r.value().collect_sources(empty);
    REQUIRE(sources.is_ok());
    REQUIRE(sources.value().size() == 1);
}

TEST_CASE("collect_source_groups preserves structure", "[project]") {
    TempDir td;
    td.write_file("Loom.toml", R"(
[package]
name = "test"
version = "0.1.0"

[[sources]]
files = ["rtl/top.sv"]
include_dirs = ["rtl/inc"]
defines = ["SYNTH"]
)");
    td.write_file("rtl/top.sv", "");

    auto r = Project::load(td.path);
    REQUIRE(r.is_ok());

    TargetSet empty;
    auto groups = r.value().collect_source_groups(empty);
    REQUIRE(groups.is_ok());
    REQUIRE(groups.value().size() == 1);
    REQUIRE(groups.value()[0].include_dirs.size() == 1);
    REQUIRE(groups.value()[0].defines.size() == 1);
    REQUIRE(groups.value()[0].defines[0] == "SYNTH");
}
