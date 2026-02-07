#include <catch2/catch.hpp>
#include <loom/source.hpp>

using namespace loom;

// ===== Valid dependencies =====

TEST_CASE("git dependency with tag", "[source]") {
    Dependency dep;
    dep.name = "uart_ip";
    dep.git = GitSource{"https://github.com/org/uart.git", "v1.3.0", {}, {}, {}};
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("git dependency with version", "[source]") {
    Dependency dep;
    dep.name = "axi_bus";
    dep.git = GitSource{"https://github.com/org/axi.git", {}, ">=2.0.0, <3.0.0", {}, {}};
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("git dependency with rev", "[source]") {
    Dependency dep;
    dep.name = "crypto";
    dep.git = GitSource{"ssh://git@internal.corp/crypto.git", {}, {}, "abc123def456", {}};
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("git dependency with branch", "[source]") {
    Dependency dep;
    dep.name = "spi_dev";
    dep.git = GitSource{"https://github.com/org/spi.git", {}, {}, {}, "develop"};
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("path dependency", "[source]") {
    Dependency dep;
    dep.name = "my_testbench";
    dep.path = PathSource{"../testbench"};
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("workspace dependency", "[source]") {
    Dependency dep;
    dep.name = "common_cells";
    dep.workspace = true;
    REQUIRE(dep.validate().is_ok());
}

TEST_CASE("member dependency", "[source]") {
    Dependency dep;
    dep.name = "sibling_ip";
    dep.member = true;
    REQUIRE(dep.validate().is_ok());
}

// ===== Validation errors =====

TEST_CASE("no source error", "[source]") {
    Dependency dep;
    dep.name = "orphan";
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Dependency);
}

TEST_CASE("multiple sources error", "[source]") {
    Dependency dep;
    dep.name = "confused";
    dep.git = GitSource{"https://example.com/repo.git", "v1.0.0", {}, {}, {}};
    dep.path = PathSource{"../local"};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("multiple sources") != std::string::npos);
}

TEST_CASE("git and workspace conflict", "[source]") {
    Dependency dep;
    dep.name = "conflict";
    dep.git = GitSource{"https://example.com/repo.git", "v1.0.0", {}, {}, {}};
    dep.workspace = true;
    REQUIRE(dep.validate().is_err());
}

TEST_CASE("git empty URL error", "[source]") {
    Dependency dep;
    dep.name = "bad_url";
    dep.git = GitSource{"", "v1.0.0", {}, {}, {}};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("empty git URL") != std::string::npos);
}

TEST_CASE("git no ref error", "[source]") {
    Dependency dep;
    dep.name = "no_ref";
    dep.git = GitSource{"https://example.com/repo.git", {}, {}, {}, {}};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("no ref") != std::string::npos);
}

TEST_CASE("git multiple refs error", "[source]") {
    Dependency dep;
    dep.name = "multi_ref";
    dep.git = GitSource{"https://example.com/repo.git", "v1.0.0", {}, {}, "main"};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("multiple refs") != std::string::npos);
}

TEST_CASE("git invalid version constraint error", "[source]") {
    Dependency dep;
    dep.name = "bad_ver";
    dep.git = GitSource{"https://example.com/repo.git", {}, "not_a_version", {}, {}};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("invalid version constraint") != std::string::npos);
}

TEST_CASE("path empty error", "[source]") {
    Dependency dep;
    dep.name = "empty_path";
    dep.path = PathSource{""};
    auto r = dep.validate();
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("empty path") != std::string::npos);
}
