#include <catch2/catch.hpp>
#include <loom/swap.hpp>

using namespace loom;

// ---- Basic substitution (4) ----

TEST_CASE("swap single variable", "[swap]") {
    SwapMap vars = {{"name", "adder"}};
    auto res = swap_template("module {{ name }};", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "module adder;");
}

TEST_CASE("swap multiple variables", "[swap]") {
    SwapMap vars = {{"top", "chip"}, {"lib", "mylib"}};
    auto res = swap_template("{{top}}/{{lib}}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "chip/mylib");
}

TEST_CASE("swap no variables in template", "[swap]") {
    SwapMap vars = {{"unused", "val"}};
    auto res = swap_template("plain text", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "plain text");
}

TEST_CASE("swap empty template", "[swap]") {
    SwapMap vars;
    auto res = swap_template("", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value().empty());
}

// ---- Whitespace handling (3) ----

TEST_CASE("swap no space around var name", "[swap]") {
    SwapMap vars = {{"x", "1"}};
    auto res = swap_template("{{x}}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "1");
}

TEST_CASE("swap asymmetric whitespace", "[swap]") {
    SwapMap vars = {{"x", "1"}};
    auto res = swap_template("{{x }}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "1");
}

TEST_CASE("swap extra whitespace", "[swap]") {
    SwapMap vars = {{"x", "1"}};
    auto res = swap_template("{{   x   }}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "1");
}

// ---- Errors (4) ----

TEST_CASE("swap undefined variable error", "[swap]") {
    SwapMap vars = {{"a", "1"}};
    auto res = swap_template("{{ b }}", vars);
    REQUIRE(res.is_err());
    REQUIRE(res.error().code == LoomError::NotFound);
}

TEST_CASE("swap unclosed braces error", "[swap]") {
    SwapMap vars;
    auto res = swap_template("{{ oops", vars);
    REQUIRE(res.is_err());
    REQUIRE(res.error().code == LoomError::Parse);
}

TEST_CASE("swap empty variable name error", "[swap]") {
    SwapMap vars;
    auto res = swap_template("{{  }}", vars);
    REQUIRE(res.is_err());
    REQUIRE(res.error().code == LoomError::Parse);
}

TEST_CASE("swap undefined var hint lists available", "[swap]") {
    SwapMap vars = {{"alpha", "1"}, {"beta", "2"}};
    auto res = swap_template("{{ gamma }}", vars);
    REQUIRE(res.is_err());
    REQUIRE(res.error().code == LoomError::NotFound);
    // Hint should mention available vars
    REQUIRE(res.error().hint.find("alpha") != std::string::npos);
    REQUIRE(res.error().hint.find("beta") != std::string::npos);
}

// ---- Escaping / edge cases (3) ----

TEST_CASE("swap escaped opening braces", "[swap]") {
    SwapMap vars = {{"x", "1"}};
    auto res = swap_template("\\{{ not a var }}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "{{ not a var }}");
}

TEST_CASE("swap value containing braces no recursion", "[swap]") {
    SwapMap vars = {{"x", "{{ y }}"}};
    auto res = swap_template("val={{ x }}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "val={{ y }}");
}

TEST_CASE("swap adjacent variables", "[swap]") {
    SwapMap vars = {{"a", "hello"}, {"b", "world"}};
    auto res = swap_template("{{a}}{{b}}", vars);
    REQUIRE(res.is_ok());
    REQUIRE(res.value() == "helloworld");
}

// ---- Lenient mode (2) ----

TEST_CASE("swap lenient leaves undefined as-is", "[swap]") {
    SwapMap vars = {{"a", "1"}};
    auto out = swap_template_lenient("{{ a }} and {{ b }}", vars);
    REQUIRE(out == "1 and {{ b }}");
}

TEST_CASE("swap lenient works when all defined", "[swap]") {
    SwapMap vars = {{"x", "hello"}, {"y", "world"}};
    auto out = swap_template_lenient("{{ x }} {{ y }}", vars);
    REQUIRE(out == "hello world");
}
