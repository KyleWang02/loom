#include <catch2/catch.hpp>
#include <loom/config.hpp>

using namespace loom;

// ===== Parsing =====

TEST_CASE("parse config with lint rules", "[config]") {
    auto r = Config::parse(R"(
[lint]
blocking-in-ff = "error"
naming-module = "off"
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().lint.rules.at("blocking-in-ff") == "error");
    REQUIRE(r.value().lint.rules.at("naming-module") == "off");
}

TEST_CASE("parse config with build section", "[config]") {
    auto r = Config::parse(R"(
[build]
pre-lint = true
lint-fatal = true
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().build.pre_lint == true);
    REQUIRE(r.value().build.lint_fatal == true);
}

TEST_CASE("parse config with targets", "[config]") {
    auto r = Config::parse(R"(
[targets.sim]
tool = "verilator"
action = "simulate"
)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().targets.count("sim") == 1);
    REQUIRE(r.value().targets.at("sim").tool == "verilator");
}

TEST_CASE("parse empty config", "[config]") {
    auto r = Config::parse("");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().lint.rules.empty());
    REQUIRE(r.value().targets.empty());
}

TEST_CASE("parse invalid TOML config", "[config]") {
    auto r = Config::parse("not valid [toml");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

// ===== Merge =====

TEST_CASE("merge overrides lint rules", "[config]") {
    auto base = Config::parse(R"(
[lint]
blocking-in-ff = "warn"
case-missing-default = "warn"
)").value();

    auto overlay = Config::parse(R"(
[lint]
blocking-in-ff = "error"
naming-module = "warn"
)").value();

    base.merge(overlay);
    REQUIRE(base.lint.rules.at("blocking-in-ff") == "error");  // overridden
    REQUIRE(base.lint.rules.at("case-missing-default") == "warn"); // preserved
    REQUIRE(base.lint.rules.at("naming-module") == "warn");  // added
}

TEST_CASE("merge overrides targets", "[config]") {
    auto base = Config::parse(R"(
[targets.sim]
tool = "icarus"
action = "simulate"
)").value();

    auto overlay = Config::parse(R"(
[targets.sim]
tool = "verilator"
action = "simulate"
)").value();

    base.merge(overlay);
    REQUIRE(base.targets.at("sim").tool == "verilator");
}

TEST_CASE("merge adds new targets", "[config]") {
    auto base = Config::parse(R"(
[targets.sim]
tool = "icarus"
action = "simulate"
)").value();

    auto overlay = Config::parse(R"(
[targets.synth]
tool = "vivado-synth"
action = "synthesize"
)").value();

    base.merge(overlay);
    REQUIRE(base.targets.size() == 2);
    REQUIRE(base.targets.count("sim") == 1);
    REQUIRE(base.targets.count("synth") == 1);
}

// ===== Effective config =====

TEST_CASE("effective config layering", "[config]") {
    auto global = Config::parse(R"(
[lint]
blocking-in-ff = "warn"
case-missing-default = "warn"

[build]
pre-lint = false
lint-fatal = false
)").value();

    auto workspace = Config::parse(R"(
[lint]
blocking-in-ff = "error"

[build]
pre-lint = true
)").value();

    auto local = Config::parse(R"(
[lint]
case-missing-default = "off"
)").value();

    auto eff = Config::effective(global, workspace, local);

    // blocking-in-ff: global=warn, workspace=error -> error
    REQUIRE(eff.lint.rules.at("blocking-in-ff") == "error");
    // case-missing-default: global=warn, local=off -> off
    REQUIRE(eff.lint.rules.at("case-missing-default") == "off");
    // pre-lint: global=false, workspace=true -> true
    REQUIRE(eff.build.pre_lint == true);
}

TEST_CASE("effective with no layers", "[config]") {
    auto eff = Config::effective({}, {}, {});
    REQUIRE(eff.lint.rules.empty());
    REQUIRE(eff.targets.empty());
}

TEST_CASE("effective with only global", "[config]") {
    auto global = Config::parse(R"(
[lint]
blocking-in-ff = "error"
)").value();

    auto eff = Config::effective(global, {}, {});
    REQUIRE(eff.lint.rules.at("blocking-in-ff") == "error");
}

// ===== Global config path =====

TEST_CASE("global config path contains .loom", "[config]") {
    auto path = global_config_path();
    // May be empty if HOME is not set, but if set, should contain .loom
    if (!path.empty()) {
        REQUIRE(path.find(".loom/config.toml") != std::string::npos);
    }
}
