#include <catch2/catch.hpp>
#include <loom/target_expr.hpp>

using namespace loom;

// ===== Parsing tests =====

TEST_CASE("parse bare identifier", "[target_expr]") {
    auto r = TargetExpr::parse("simulation");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().kind() == TargetExpr::Identifier);
    REQUIRE(r.value().to_string() == "simulation");
}

TEST_CASE("parse wildcard", "[target_expr]") {
    auto r = TargetExpr::parse("*");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().kind() == TargetExpr::Wildcard);
    REQUIRE(r.value().to_string() == "*");
}

TEST_CASE("parse simple all", "[target_expr]") {
    auto r = TargetExpr::parse("all(sim, synth)");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().kind() == TargetExpr::All);
    REQUIRE(r.value().to_string() == "all(sim, synth)");
}

TEST_CASE("parse nested expression", "[target_expr]") {
    auto r = TargetExpr::parse("all(simulation, not(verilator))");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().kind() == TargetExpr::All);
    REQUIRE(r.value().to_string() == "all(simulation, not(verilator))");
}

TEST_CASE("parse deep nesting", "[target_expr]") {
    auto r = TargetExpr::parse("any(all(a, b), not(c))");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().kind() == TargetExpr::Any);
    REQUIRE(r.value().to_string() == "any(all(a, b), not(c))");
}

TEST_CASE("parse whitespace tolerance", "[target_expr]") {
    auto r = TargetExpr::parse("all( sim , synth )");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().to_string() == "all(sim, synth)");
}

TEST_CASE("parse roundtrip", "[target_expr]") {
    auto r1 = TargetExpr::parse("any(all(x, y), not(z))");
    REQUIRE(r1.is_ok());
    auto s1 = r1.value().to_string();

    auto r2 = TargetExpr::parse(s1);
    REQUIRE(r2.is_ok());
    REQUIRE(r2.value().to_string() == s1);
}

// ===== Parse error tests =====

TEST_CASE("parse error on empty string", "[target_expr]") {
    auto r = TargetExpr::parse("");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::InvalidArg);
}

TEST_CASE("parse error on invalid name", "[target_expr]") {
    auto r = TargetExpr::parse("123bad");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("parse error on unclosed paren", "[target_expr]") {
    auto r = TargetExpr::parse("all(sim");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("parse error on trailing garbage", "[target_expr]") {
    auto r = TargetExpr::parse("sim extra");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

// ===== Evaluation tests =====

TEST_CASE("wildcard matches everything", "[target_expr]") {
    auto expr = TargetExpr::wildcard();
    REQUIRE(expr.evaluate({}) == true);
    REQUIRE(expr.evaluate({"sim", "synth"}) == true);
}

TEST_CASE("identifier matches when in set", "[target_expr]") {
    auto expr = TargetExpr::identifier("sim");
    REQUIRE(expr.evaluate({"sim", "synth"}) == true);
}

TEST_CASE("identifier fails when not in set", "[target_expr]") {
    auto expr = TargetExpr::identifier("fpga");
    REQUIRE(expr.evaluate({"sim", "synth"}) == false);
}

TEST_CASE("all with both present", "[target_expr]") {
    auto expr = TargetExpr::all({
        TargetExpr::identifier("a"),
        TargetExpr::identifier("b")
    });
    REQUIRE(expr.evaluate({"a", "b", "c"}) == true);
}

TEST_CASE("all with one missing", "[target_expr]") {
    auto expr = TargetExpr::all({
        TargetExpr::identifier("a"),
        TargetExpr::identifier("b")
    });
    REQUIRE(expr.evaluate({"a", "c"}) == false);
}

TEST_CASE("any with one present", "[target_expr]") {
    auto expr = TargetExpr::any({
        TargetExpr::identifier("a"),
        TargetExpr::identifier("b")
    });
    REQUIRE(expr.evaluate({"b"}) == true);
}

TEST_CASE("not inverts correctly", "[target_expr]") {
    auto expr = TargetExpr::negation(TargetExpr::identifier("x"));
    REQUIRE(expr.evaluate({"x"}) == false);
    REQUIRE(expr.evaluate({"y"}) == true);
    REQUIRE(expr.evaluate({}) == true);
}

// ===== Vacuous truth tests =====

TEST_CASE("all with no children is true", "[target_expr]") {
    auto expr = TargetExpr::all({});
    REQUIRE(expr.evaluate({}) == true);
    REQUIRE(expr.evaluate({"anything"}) == true);
}

TEST_CASE("any with no children is false", "[target_expr]") {
    auto expr = TargetExpr::any({});
    REQUIRE(expr.evaluate({}) == false);
    REQUIRE(expr.evaluate({"anything"}) == false);
}

// ===== Target name validation tests =====

TEST_CASE("valid target names", "[target_expr]") {
    REQUIRE(is_valid_target_name("simulation") == true);
    REQUIRE(is_valid_target_name("fpga-synth") == true);
    REQUIRE(is_valid_target_name("test_bench") == true);
    REQUIRE(is_valid_target_name("A") == true);
    REQUIRE(is_valid_target_name("xcelium2") == true);
}

TEST_CASE("invalid target names", "[target_expr]") {
    REQUIRE(is_valid_target_name("") == false);
    REQUIRE(is_valid_target_name("123") == false);
    REQUIRE(is_valid_target_name("-start") == false);
    REQUIRE(is_valid_target_name("has space") == false);
    REQUIRE(is_valid_target_name("_leading") == false);
}

// ===== CLI target set parsing tests =====

TEST_CASE("parse target set", "[target_expr]") {
    auto r = parse_target_set("sim,synth,fpga");
    REQUIRE(r.is_ok());
    auto& s = r.value();
    REQUIRE(s.size() == 3);
    REQUIRE(s.count("sim") == 1);
    REQUIRE(s.count("synth") == 1);
    REQUIRE(s.count("fpga") == 1);
}

TEST_CASE("parse target set with whitespace", "[target_expr]") {
    auto r = parse_target_set(" sim , synth , fpga ");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 3);
}

TEST_CASE("parse target set invalid name", "[target_expr]") {
    auto r = parse_target_set("sim,123bad,fpga");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

// ===== Source group filtering tests =====

TEST_CASE("filter source groups", "[target_expr]") {
    SourceGroup g1;
    g1.files = {"always.sv"};
    // g1.target is nullopt — always included

    SourceGroup g2;
    g2.target = TargetExpr::identifier("sim");
    g2.files = {"sim_only.sv"};

    SourceGroup g3;
    g3.target = TargetExpr::identifier("synth");
    g3.files = {"synth_only.sv"};

    std::vector<SourceGroup> groups = {g1, g2, g3};
    TargetSet active = {"sim"};

    auto filtered = filter_source_groups(groups, active);
    REQUIRE(filtered.size() == 2);
    REQUIRE(filtered[0].files[0] == "always.sv");
    REQUIRE(filtered[1].files[0] == "sim_only.sv");
}
