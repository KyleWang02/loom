#include <catch2/catch.hpp>
#include <loom/glob.hpp>
#include <cstdlib>

using namespace loom;

// Resolve fixture paths relative to the source tree.
static std::string fixture_dir() {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures";
    return "../tests/fixtures";
}

// ---- Literal matching (3) ----

TEST_CASE("glob literal exact match", "[glob]") {
    REQUIRE(glob_match("foo/bar.v", "foo/bar.v"));
}

TEST_CASE("glob literal no match", "[glob]") {
    REQUIRE_FALSE(glob_match("foo/bar.v", "foo/baz.v"));
}

TEST_CASE("glob literal case sensitivity", "[glob]") {
    REQUIRE_FALSE(glob_match("Foo.v", "foo.v"));
}

// ---- Wildcards (4) ----

TEST_CASE("glob star matches filename", "[glob]") {
    REQUIRE(glob_match("src/*.sv", "src/adder.sv"));
    REQUIRE_FALSE(glob_match("src/*.sv", "src/sub/adder.sv"));
}

TEST_CASE("glob star in middle of name", "[glob]") {
    REQUIRE(glob_match("test_*_tb.sv", "test_adder_tb.sv"));
    REQUIRE_FALSE(glob_match("test_*_tb.sv", "test_adder_tb.v"));
}

TEST_CASE("glob question mark single char", "[glob]") {
    REQUIRE(glob_match("file?.v", "file1.v"));
    REQUIRE_FALSE(glob_match("file?.v", "file12.v"));
}

TEST_CASE("glob star matches empty string", "[glob]") {
    REQUIRE(glob_match("src/*.v", "src/.v"));
}

// ---- Double-star (4) ----

TEST_CASE("glob doublestar deep path", "[glob]") {
    REQUIRE(glob_match("**/*.sv", "a/b/c/d.sv"));
}

TEST_CASE("glob doublestar at start", "[glob]") {
    REQUIRE(glob_match("**/foo.v", "foo.v"));
    REQUIRE(glob_match("**/foo.v", "a/b/foo.v"));
}

TEST_CASE("glob doublestar in middle", "[glob]") {
    REQUIRE(glob_match("src/**/test.v", "src/test.v"));
    REQUIRE(glob_match("src/**/test.v", "src/a/b/test.v"));
}

TEST_CASE("glob doublestar at end", "[glob]") {
    REQUIRE(glob_match("src/**", "src/a.v"));
    REQUIRE(glob_match("src/**", "src/a/b/c.v"));
}

// ---- Character classes (3) ----

TEST_CASE("glob char class set", "[glob]") {
    REQUIRE(glob_match("[abc].v", "a.v"));
    REQUIRE(glob_match("[abc].v", "c.v"));
    REQUIRE_FALSE(glob_match("[abc].v", "d.v"));
}

TEST_CASE("glob char class range", "[glob]") {
    REQUIRE(glob_match("[a-z].v", "m.v"));
    REQUIRE_FALSE(glob_match("[a-z].v", "M.v"));
}

TEST_CASE("glob char class negation", "[glob]") {
    REQUIRE(glob_match("[!0-9].v", "a.v"));
    REQUIRE_FALSE(glob_match("[!0-9].v", "5.v"));
}

// ---- Negation / filter (2) ----

TEST_CASE("glob_is_negation", "[glob]") {
    std::string inner;
    REQUIRE(glob_is_negation("!*.bak", inner));
    REQUIRE(inner == "*.bak");

    REQUIRE_FALSE(glob_is_negation("*.sv", inner));
}

TEST_CASE("glob_filter include and exclude", "[glob]") {
    std::vector<std::string> patterns = {"**/*.sv", "!**/test_*.sv"};
    std::vector<std::string> paths = {
        "src/adder.sv",
        "src/test_adder.sv",
        "lib/mux.sv",
        "lib/test_mux.sv",
        "readme.md"
    };

    auto result = glob_filter(patterns, paths);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == "src/adder.sv");
    REQUIRE(result[1] == "lib/mux.sv");
}

// ---- Filesystem (2) ----

TEST_CASE("glob_expand finds fixture file", "[glob]") {
    auto res = glob_expand("*.v", fixture_dir());
    REQUIRE(res.is_ok());
    auto& files = res.value();
    REQUIRE(files.size() >= 1);
    // simple_module.v should be in there
    bool found = false;
    for (const auto& f : files) {
        if (f == "simple_module.v") found = true;
    }
    REQUIRE(found);
}

TEST_CASE("glob_expand nonexistent dir returns IO error", "[glob]") {
    auto res = glob_expand("*.v", "/nonexistent_dir_xyz_123");
    REQUIRE(res.is_err());
    REQUIRE(res.error().code == LoomError::IO);
}
