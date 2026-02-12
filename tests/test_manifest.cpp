#include <catch2/catch.hpp>
#include <loom/manifest.hpp>
#include <chrono>
#include <cstdio>
#include <sstream>

using namespace loom;

static std::string fixture_dir() {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures";
    return "../tests/fixtures";
}

// ===== Package section =====

TEST_CASE("parse package section", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "my-soc"
version = "1.0.0"
top = "soc_top"
authors = ["Alice", "Bob"]
)");
    REQUIRE(r.is_ok());
    auto& m = r.value();
    REQUIRE(m.package.name == "my-soc");
    REQUIRE(m.package.version == "1.0.0");
    REQUIRE(m.package.top == "soc_top");
    REQUIRE(m.package.authors.size() == 2);
    REQUIRE(m.package.authors[0] == "Alice");
}

// ===== Dependencies section =====

TEST_CASE("parse git dependency with tag", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[dependencies]
uart_ip = { git = "https://github.com/org/uart.git", tag = "v1.3.0" }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().dependencies.size() == 1);
    auto& dep = r.value().dependencies[0];
    REQUIRE(dep.name == "uart_ip");
    REQUIRE(dep.git.has_value());
    REQUIRE(dep.git->url == "https://github.com/org/uart.git");
    REQUIRE(dep.git->tag.value() == "v1.3.0");
}

TEST_CASE("parse path dependency", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[dependencies]
my_tb = { path = "../testbench" }
)");
    REQUIRE(r.is_ok());
    auto& dep = r.value().dependencies[0];
    REQUIRE(dep.path.has_value());
    REQUIRE(dep.path->path == "../testbench");
}

TEST_CASE("parse workspace dependency", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[dependencies]
common = { workspace = true }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().dependencies[0].workspace == true);
}

TEST_CASE("invalid dependency detected", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[dependencies]
bad = { git = "https://example.com/r.git" }
)");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Dependency);
}

// ===== Sources section =====

TEST_CASE("parse sources without target", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[[sources]]
files = ["rtl/top.sv", "rtl/bus.sv"]
include_dirs = ["rtl/include"]
defines = ["SYNTHESIS"]
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().sources.size() == 1);
    auto& sg = r.value().sources[0];
    REQUIRE_FALSE(sg.target.has_value());
    REQUIRE(sg.files.size() == 2);
    REQUIRE(sg.include_dirs.size() == 1);
    REQUIRE(sg.defines.size() == 1);
    REQUIRE(sg.defines[0] == "SYNTHESIS");
}

TEST_CASE("parse sources with target expression", "[manifest]") {
    auto r = Manifest::parse(R"TOML(
[package]
name = "test"
version = "0.1.0"

[[sources]]
target = "all(simulation, not(verilator))"
files = ["tb/model.sv"]
)TOML");
    REQUIRE(r.is_ok());
    auto& sg = r.value().sources[0];
    REQUIRE(sg.target.has_value());
    REQUIRE(sg.target->kind() == TargetExpr::All);
    REQUIRE(sg.target->to_string() == "all(simulation, not(verilator))");
}

TEST_CASE("parse multiple source groups", "[manifest]") {
    auto r = Manifest::parse(R"(
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
    REQUIRE(r.is_ok());
    REQUIRE(r.value().sources.size() == 3);
}

// ===== Targets section =====

TEST_CASE("parse target config", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[targets.sim]
tool = "verilator"
action = "simulate"

[targets.sim.options]
waveform = true
compile_args = ["--timing", "-Wall"]
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().targets.size() == 1);
    REQUIRE(r.value().targets.count("sim") == 1);
    auto& tc = r.value().targets.at("sim");
    REQUIRE(tc.tool == "verilator");
    REQUIRE(tc.action == "simulate");
    REQUIRE(tc.options.at("waveform") == "true");
    REQUIRE(tc.options.at("compile_args") == "--timing,-Wall");
}

// ===== Lint section =====

TEST_CASE("parse lint config", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[lint]
blocking-in-ff = "error"
naming-module = "warn"

[lint.naming]
module_pattern = "[a-z][a-z0-9_]*"
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().lint.rules.at("blocking-in-ff") == "error");
    REQUIRE(r.value().lint.rules.at("naming-module") == "warn");
    REQUIRE(r.value().lint.naming.at("module_pattern") == "[a-z][a-z0-9_]*");
}

// ===== Build section =====

TEST_CASE("parse build config", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[build]
pre-lint = true
lint-fatal = false
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().build.pre_lint == true);
    REQUIRE(r.value().build.lint_fatal == false);
}

// ===== Workspace section =====

TEST_CASE("parse workspace config", "[manifest]") {
    auto r = Manifest::parse(R"(
[workspace]
members = ["ip/*", "soc/top"]
exclude = ["ip/deprecated"]
default-members = ["soc/top"]

[workspace.dependencies]
common = { git = "https://github.com/org/common.git", tag = "v1.37.0" }
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().is_workspace());
    auto& ws = r.value().workspace.value();
    REQUIRE(ws.members.size() == 2);
    REQUIRE(ws.members[0] == "ip/*");
    REQUIRE(ws.exclude.size() == 1);
    REQUIRE(ws.default_members.size() == 1);
    REQUIRE(ws.dependencies.size() == 1);
    REQUIRE(ws.dependencies[0].git->tag.value() == "v1.37.0");
}

TEST_CASE("non-workspace manifest", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "simple"
version = "0.1.0"
)");
    REQUIRE(r.is_ok());
    REQUIRE_FALSE(r.value().is_workspace());
}

// ===== Load from file =====

TEST_CASE("load manifest from fixture file", "[manifest]") {
    auto path = fixture_dir() + "/Loom.toml.example";
    auto r = Manifest::load(path);
    REQUIRE(r.is_ok());
    auto& m = r.value();
    REQUIRE(m.package.name == "my-soc");
    REQUIRE(m.package.version == "1.0.0");
    REQUIRE(m.dependencies.size() == 4);
    REQUIRE(m.sources.size() == 3);
    REQUIRE(m.targets.size() == 2);
    REQUIRE(m.build.pre_lint == true);
}

// ===== Error cases =====

TEST_CASE("invalid TOML syntax", "[manifest]") {
    auto r = Manifest::parse("this is not [valid toml");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("invalid target expression in sources", "[manifest]") {
    auto r = Manifest::parse(R"TOML(
[package]
name = "test"
version = "0.1.0"

[[sources]]
target = "all(unclosed"
files = ["foo.sv"]
)TOML");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("load nonexistent file", "[manifest]") {
    auto r = Manifest::load("/nonexistent/Loom.toml");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::IO);
}

// ===== Minimal manifest =====

TEST_CASE("minimal manifest with just package", "[manifest]") {
    auto r = Manifest::parse(R"(
[package]
name = "minimal"
version = "0.1.0"
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().dependencies.empty());
    REQUIRE(r.value().sources.empty());
    REQUIRE(r.value().targets.empty());
}

// ===== Roundtrip (to_toml / save) =====

TEST_CASE("roundtrip — minimal manifest", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "roundtrip"
version = "2.0.0"
)");
    REQUIRE(r1.is_ok());
    auto& m1 = r1.value();

    // Serialize and re-parse
    std::ostringstream ss;
    ss << m1.to_toml();
    auto r2 = Manifest::parse(ss.str());
    REQUIRE(r2.is_ok());
    auto& m2 = r2.value();
    CHECK(m2.package.name == "roundtrip");
    CHECK(m2.package.version == "2.0.0");
}

TEST_CASE("roundtrip — manifest with dependencies", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[dependencies]
uart = { git = "https://example.com/uart.git", tag = "v1.0" }
spi = { path = "../spi" }
)");
    REQUIRE(r1.is_ok());
    auto& m1 = r1.value();

    std::ostringstream ss;
    ss << m1.to_toml();
    auto r2 = Manifest::parse(ss.str());
    REQUIRE(r2.is_ok());
    auto& m2 = r2.value();
    REQUIRE(m2.dependencies.size() == 2);

    // Find deps by name (order may differ)
    const Dependency* uart = nullptr;
    const Dependency* spi = nullptr;
    for (auto& d : m2.dependencies) {
        if (d.name == "uart") uart = &d;
        if (d.name == "spi") spi = &d;
    }
    REQUIRE(uart != nullptr);
    REQUIRE(spi != nullptr);
    CHECK(uart->git->url == "https://example.com/uart.git");
    CHECK(uart->git->tag.value() == "v1.0");
    CHECK(spi->path->path == "../spi");
}

TEST_CASE("roundtrip — manifest with lint config", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[lint]
blocking-in-ff = "off"
casex-usage = "error"

[lint.naming]
module = "^[a-z][a-z0-9_]*$"
)");
    REQUIRE(r1.is_ok());
    auto& m1 = r1.value();

    std::ostringstream ss;
    ss << m1.to_toml();
    auto r2 = Manifest::parse(ss.str());
    REQUIRE(r2.is_ok());
    auto& m2 = r2.value();
    CHECK(m2.lint.rules.at("blocking-in-ff") == "off");
    CHECK(m2.lint.rules.at("casex-usage") == "error");
    CHECK(m2.lint.naming.at("module") == "^[a-z][a-z0-9_]*$");
}

TEST_CASE("roundtrip — manifest with build config", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "test"
version = "0.1.0"

[build]
pre-lint = true
lint-fatal = true
)");
    REQUIRE(r1.is_ok());

    std::ostringstream ss;
    ss << r1.value().to_toml();
    auto r2 = Manifest::parse(ss.str());
    REQUIRE(r2.is_ok());
    CHECK(r2.value().build.pre_lint);
    CHECK(r2.value().build.lint_fatal);
}

TEST_CASE("roundtrip — manifest with workspace", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "ws-root"
version = "0.1.0"

[workspace]
members = ["core", "tb"]
exclude = ["vendor"]
)");
    REQUIRE(r1.is_ok());

    std::ostringstream ss;
    ss << r1.value().to_toml();
    auto r2 = Manifest::parse(ss.str());
    REQUIRE(r2.is_ok());
    REQUIRE(r2.value().workspace.has_value());
    CHECK(r2.value().workspace->members.size() == 2);
    CHECK(r2.value().workspace->exclude.size() == 1);
}

TEST_CASE("save — writes to file and reloads", "[manifest]") {
    auto r1 = Manifest::parse(R"(
[package]
name = "saved"
version = "3.0.0"
)");
    REQUIRE(r1.is_ok());

    auto tmp_path = std::string("/tmp/loom_test_manifest_") +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".toml";
    auto status = r1.value().save(tmp_path);
    REQUIRE(status.is_ok());

    auto r2 = Manifest::load(tmp_path);
    REQUIRE(r2.is_ok());
    CHECK(r2.value().package.name == "saved");
    CHECK(r2.value().package.version == "3.0.0");

    // Cleanup
    std::remove(tmp_path.c_str());
}
