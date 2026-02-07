#include <catch2/catch.hpp>
#include <loom/local_override.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace loom;
namespace fs = std::filesystem;

// RAII temp directory
struct TempDir {
    fs::path path;

    TempDir() {
        const char* src = std::getenv("LOOM_SOURCE_DIR");
        fs::path base = src ? fs::path(src) / "build" : fs::temp_directory_path();
        path = base / ("loom_lo_test_" + std::to_string(
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

// ===== Parsing: path overrides =====

TEST_CASE("parse path override", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
axi_crossbar = { path = "../axi/crossbar" }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().count() == 1);
    REQUIRE(r.value().has_override("axi_crossbar"));
    auto* src = r.value().get_override("axi_crossbar");
    REQUIRE(src != nullptr);
    REQUIRE(src->kind == OverrideSource::Kind::Path);
    REQUIRE(src->path == "../axi/crossbar");
}

// ===== Parsing: git overrides =====

TEST_CASE("parse git override", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
common_cells = { git = "git@github.com:org/common.git", branch = "fix-mux" }
)");
    REQUIRE(r.is_ok());
    auto* src = r.value().get_override("common_cells");
    REQUIRE(src != nullptr);
    REQUIRE(src->kind == OverrideSource::Kind::Git);
    REQUIRE(src->url == "git@github.com:org/common.git");
    REQUIRE(src->branch == "fix-mux");
    REQUIRE(src->tag.empty());
    REQUIRE(src->rev.empty());
}

TEST_CASE("parse git override with tag", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
my_ip = { git = "https://example.com/ip.git", tag = "v2.0.0" }
)");
    REQUIRE(r.is_ok());
    auto* src = r.value().get_override("my_ip");
    REQUIRE(src->tag == "v2.0.0");
}

TEST_CASE("parse git override with rev", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
my_ip = { git = "https://example.com/ip.git", rev = "abc123" }
)");
    REQUIRE(r.is_ok());
    auto* src = r.value().get_override("my_ip");
    REQUIRE(src->rev == "abc123");
}

// ===== Parsing: multiple overrides =====

TEST_CASE("parse multiple overrides", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
axi = { path = "../axi" }
common = { git = "https://example.com/common.git", branch = "dev" }
utils = { path = "../utils" }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().count() == 3);
    REQUIRE(r.value().has_override("axi"));
    REQUIRE(r.value().has_override("common"));
    REQUIRE(r.value().has_override("utils"));
}

// ===== Parsing: empty/missing =====

TEST_CASE("parse empty overrides", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().empty());
}

TEST_CASE("parse no overrides section", "[local_override]") {
    auto r = LocalOverrides::parse("");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().empty());
}

// ===== Parsing: errors =====

TEST_CASE("parse rejects both path and git", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
bad = { path = "../x", git = "https://example.com/x.git" }
)");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("parse rejects neither path nor git", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
bad = { branch = "main" }
)");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("parse rejects invalid TOML", "[local_override]") {
    auto r = LocalOverrides::parse("this is not [valid toml");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

// ===== Query methods =====

TEST_CASE("get_override returns null for unknown", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
axi = { path = "../axi" }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().get_override("nonexistent") == nullptr);
    REQUIRE_FALSE(r.value().has_override("nonexistent"));
}

// ===== Validation =====

TEST_CASE("validate path override - valid", "[local_override]") {
    TempDir td;
    td.write_file("dep/Loom.toml", R"(
[package]
name = "dep"
version = "0.1.0"
)");

    auto r = LocalOverrides::parse(
        "[overrides]\ndep = { path = \"" + (td.path / "dep").string() + "\" }\n");
    REQUIRE(r.is_ok());
    auto status = r.value().validate();
    REQUIRE(status.is_ok());
}

TEST_CASE("validate path override - missing dir", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
dep = { path = "/nonexistent/dir/nowhere" }
)");
    REQUIRE(r.is_ok());
    auto status = r.value().validate();
    REQUIRE(status.is_err());
    REQUIRE(status.error().code == LoomError::IO);
}

TEST_CASE("validate path override - no manifest", "[local_override]") {
    TempDir td;
    // Create dir but no Loom.toml
    fs::create_directories(td.path / "dep");

    auto r = LocalOverrides::parse(
        "[overrides]\ndep = { path = \"" + (td.path / "dep").string() + "\" }\n");
    REQUIRE(r.is_ok());
    auto status = r.value().validate();
    REQUIRE(status.is_err());
    REQUIRE(status.error().code == LoomError::Manifest);
}

TEST_CASE("validate git override - valid", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
dep = { git = "https://github.com/org/dep.git", branch = "main" }
)");
    REQUIRE(r.is_ok());
    auto status = r.value().validate();
    REQUIRE(status.is_ok());
}

// ===== Discovery =====

TEST_CASE("discover finds Loom.local", "[local_override]") {
    TempDir td;
    td.write_file("Loom.local", R"(
[overrides]
axi = { path = "../axi" }
)");

    auto r = discover_local_overrides(td.path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().count() == 1);
    REQUIRE(r.value().has_override("axi"));
}

TEST_CASE("discover returns empty when no Loom.local", "[local_override]") {
    TempDir td;

    auto r = discover_local_overrides(td.path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().empty());
}

// ===== should_suppress_overrides =====

TEST_CASE("suppress with flag", "[local_override]") {
    REQUIRE(should_suppress_overrides(true));
}

TEST_CASE("suppress with env var", "[local_override]") {
    // Save and set
    const char* old = std::getenv("LOOM_NO_LOCAL");
    #ifdef _WIN32
    _putenv_s("LOOM_NO_LOCAL", "1");
    #else
    setenv("LOOM_NO_LOCAL", "1", 1);
    #endif

    REQUIRE(should_suppress_overrides(false));

    // Restore
    #ifdef _WIN32
    if (old) _putenv_s("LOOM_NO_LOCAL", old);
    else _putenv_s("LOOM_NO_LOCAL", "");
    #else
    if (old) setenv("LOOM_NO_LOCAL", old, 1);
    else unsetenv("LOOM_NO_LOCAL");
    #endif
}

TEST_CASE("no suppress by default", "[local_override]") {
    // Ensure env var not set
    const char* old = std::getenv("LOOM_NO_LOCAL");
    #ifdef _WIN32
    _putenv_s("LOOM_NO_LOCAL", "");
    #else
    unsetenv("LOOM_NO_LOCAL");
    #endif

    REQUIRE_FALSE(should_suppress_overrides(false));

    // Restore
    #ifdef _WIN32
    if (old) _putenv_s("LOOM_NO_LOCAL", old);
    #else
    if (old) setenv("LOOM_NO_LOCAL", old, 1);
    #endif
}

// ===== warn_active (smoke test - just verify it doesn't crash) =====

TEST_CASE("warn_active does not crash", "[local_override]") {
    auto r = LocalOverrides::parse(R"(
[overrides]
axi = { path = "../axi" }
common = { git = "https://example.com/common.git", branch = "dev" }
bare_git = { git = "https://example.com/bare.git" }
)");
    REQUIRE(r.is_ok());
    // Just verify it doesn't crash/throw
    r.value().warn_active();
}
