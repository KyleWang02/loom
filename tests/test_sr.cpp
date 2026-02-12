#include <catch2/catch.hpp>
#include <loom/sr.hpp>
#include <loom/sha256.hpp>

using namespace loom;

// ---- Helper: build ParseResult from inline source descriptions ----

static ParseResult make_parse_result(
    std::initializer_list<std::pair<std::string, DesignUnitKind>> units,
    int start_depth = 0) {
    ParseResult pr;
    int line = 1;
    for (auto& [name, kind] : units) {
        DesignUnit u;
        u.name = name;
        u.kind = kind;
        u.start_line = line;
        u.end_line = line + 10;
        u.depth = start_depth;
        line += 20;
        pr.units.push_back(std::move(u));
    }
    return pr;
}

static ParseResult make_module(const std::string& name, int line = 1) {
    ParseResult pr;
    DesignUnit u;
    u.name = name;
    u.kind = DesignUnitKind::Module;
    u.start_line = line;
    u.depth = 0;
    pr.units.push_back(std::move(u));
    return pr;
}

// ---- Collision detection tests ----

TEST_CASE("sr: no collisions when names are unique", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["a.v"] = make_module("mod_a");
    prs["b.v"] = make_module("mod_b");
    prs["c.v"] = make_module("mod_c");

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.empty());
}

TEST_CASE("sr: detects collision between two files", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["ip_a/counter.v"] = make_module("counter", 5);
    prs["ip_b/counter.v"] = make_module("counter", 10);

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.size() == 1);
    CHECK(collisions[0].name == "counter");
    CHECK(collisions[0].origins.size() == 2);
    // Origins should be sorted by file path
    CHECK(collisions[0].origins[0].file == "ip_a/counter.v");
    CHECK(collisions[0].origins[1].file == "ip_b/counter.v");
}

TEST_CASE("sr: detects multiple collisions", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["a.v"] = make_parse_result({{"fifo", DesignUnitKind::Module},
                                     {"arbiter", DesignUnitKind::Module}});
    prs["b.v"] = make_parse_result({{"fifo", DesignUnitKind::Module},
                                     {"arbiter", DesignUnitKind::Module}});

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.size() == 2);
    // Sorted by name
    CHECK(collisions[0].name == "arbiter");
    CHECK(collisions[1].name == "fifo");
}

TEST_CASE("sr: ignores nested units (depth > 0)", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["a.v"] = make_module("counter");

    // b.v has "counter" but nested (depth=1)
    ParseResult pr;
    DesignUnit u;
    u.name = "counter";
    u.kind = DesignUnitKind::Module;
    u.start_line = 1;
    u.depth = 1;
    pr.units.push_back(std::move(u));
    prs["b.v"] = std::move(pr);

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.empty());
}

TEST_CASE("sr: three-way collision", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["x/uart.v"] = make_module("uart");
    prs["y/uart.v"] = make_module("uart");
    prs["z/uart.v"] = make_module("uart");

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.size() == 1);
    CHECK(collisions[0].origins.size() == 3);
}

TEST_CASE("sr: collision between different unit kinds", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["a.sv"] = make_parse_result({{"common", DesignUnitKind::Package}});
    prs["b.sv"] = make_parse_result({{"common", DesignUnitKind::Package}});

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.size() == 1);
    CHECK(collisions[0].name == "common");
}

TEST_CASE("sr: empty parse results", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.empty());
}

TEST_CASE("sr: files with no units", "[sr]") {
    std::unordered_map<std::string, ParseResult> prs;
    prs["a.v"] = ParseResult{};
    prs["b.v"] = ParseResult{};

    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.empty());
}

// ---- Name mangling tests ----

TEST_CASE("sr: mangle produces deterministic names", "[sr]") {
    auto m1 = SymbolRemapper::mangle("counter", "ip_a/counter.v");
    auto m2 = SymbolRemapper::mangle("counter", "ip_a/counter.v");
    REQUIRE(m1 == m2);  // Same input = same output
}

TEST_CASE("sr: mangle produces different names for different qualifiers", "[sr]") {
    auto m1 = SymbolRemapper::mangle("counter", "ip_a/counter.v");
    auto m2 = SymbolRemapper::mangle("counter", "ip_b/counter.v");
    REQUIRE(m1 != m2);
}

TEST_CASE("sr: mangle preserves original name as prefix", "[sr]") {
    auto mangled = SymbolRemapper::mangle("fifo", "src/fifo.v");
    REQUIRE(mangled.substr(0, 5) == "fifo_");
    REQUIRE(mangled.size() == 5 + 8);  // "fifo_" + 8 hex chars
}

TEST_CASE("sr: mangle uses SHA-256 suffix", "[sr]") {
    auto mangled = SymbolRemapper::mangle("test", "file.v");
    std::string suffix = mangled.substr(5);  // after "test_"
    REQUIRE(suffix.size() == 8);
    // Verify suffix is hex
    for (char c : suffix) {
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
}

// ---- Remap table tests ----

TEST_CASE("sr: build_remap_table creates per-file mappings", "[sr]") {
    std::vector<SymbolCollision> collisions = {{
        "counter",
        {{"ip_a/counter.v", DesignUnitKind::Module, 1},
         {"ip_b/counter.v", DesignUnitKind::Module, 1}}
    }};

    auto table = SymbolRemapper::build_remap_table(collisions);
    REQUIRE(table.size() == 2);
    REQUIRE(table.count("ip_a/counter.v") == 1);
    REQUIRE(table.count("ip_b/counter.v") == 1);

    // Each file maps "counter" to a different mangled name
    auto& map_a = table["ip_a/counter.v"];
    auto& map_b = table["ip_b/counter.v"];
    REQUIRE(map_a.count("counter") == 1);
    REQUIRE(map_b.count("counter") == 1);
    CHECK(map_a["counter"] != map_b["counter"]);

    // Both mangled names start with "counter_"
    CHECK(map_a["counter"].substr(0, 8) == "counter_");
    CHECK(map_b["counter"].substr(0, 8) == "counter_");
}

TEST_CASE("sr: build_remap_table handles multiple collisions", "[sr]") {
    std::vector<SymbolCollision> collisions = {
        {"fifo", {{"a.v", DesignUnitKind::Module, 1},
                  {"b.v", DesignUnitKind::Module, 1}}},
        {"arbiter", {{"a.v", DesignUnitKind::Module, 20},
                     {"c.v", DesignUnitKind::Module, 1}}}
    };

    auto table = SymbolRemapper::build_remap_table(collisions);
    // a.v has both fifo and arbiter collisions
    REQUIRE(table["a.v"].size() == 2);
    REQUIRE(table["b.v"].size() == 1);
    REQUIRE(table["c.v"].size() == 1);
}

// ---- Token remapping tests ----

TEST_CASE("sr: remap_tokens replaces identifiers", "[sr]") {
    std::vector<VerilogToken> tokens = {
        {VerilogTokenType::KwModule, "module", {"test.v", 1, 1}},
        {VerilogTokenType::Identifier, "counter", {"test.v", 1, 8}},
        {VerilogTokenType::Semicolon, ";", {"test.v", 1, 15}},
        {VerilogTokenType::KwEndmodule, "endmodule", {"test.v", 2, 1}},
    };

    SymbolMap mapping = {{"counter", "counter_abc12345"}};
    SymbolRemapper::remap_tokens(tokens, mapping);

    CHECK(tokens[0].text == "module");      // keyword unchanged
    CHECK(tokens[1].text == "counter_abc12345");  // identifier remapped
    CHECK(tokens[2].text == ";");           // punctuation unchanged
}

TEST_CASE("sr: remap_tokens ignores non-matching identifiers", "[sr]") {
    std::vector<VerilogToken> tokens = {
        {VerilogTokenType::Identifier, "fifo", {"test.v", 1, 1}},
        {VerilogTokenType::Identifier, "arbiter", {"test.v", 1, 6}},
    };

    SymbolMap mapping = {{"counter", "counter_abc12345"}};
    SymbolRemapper::remap_tokens(tokens, mapping);

    CHECK(tokens[0].text == "fifo");
    CHECK(tokens[1].text == "arbiter");
}

TEST_CASE("sr: remap_tokens handles empty mapping", "[sr]") {
    std::vector<VerilogToken> tokens = {
        {VerilogTokenType::Identifier, "counter", {"test.v", 1, 1}},
    };

    SymbolMap mapping;
    SymbolRemapper::remap_tokens(tokens, mapping);

    CHECK(tokens[0].text == "counter");
}

TEST_CASE("sr: remap_tokens handles empty token stream", "[sr]") {
    std::vector<VerilogToken> tokens;
    SymbolMap mapping = {{"counter", "counter_xyz"}};
    SymbolRemapper::remap_tokens(tokens, mapping);
    CHECK(tokens.empty());
}

TEST_CASE("sr: remap_tokens skips keywords and strings", "[sr]") {
    std::vector<VerilogToken> tokens = {
        {VerilogTokenType::KwModule, "module", {"test.v", 1, 1}},
        {VerilogTokenType::StringLiteral, "\"counter\"", {"test.v", 1, 8}},
        {VerilogTokenType::Number, "42", {"test.v", 1, 20}},
    };

    SymbolMap mapping = {{"module", "mod_remap"}, {"counter", "cnt_remap"}};
    SymbolRemapper::remap_tokens(tokens, mapping);

    CHECK(tokens[0].text == "module");          // keyword not touched
    CHECK(tokens[1].text == "\"counter\"");     // string not touched
    CHECK(tokens[2].text == "42");              // number not touched
}

// ---- Parse result remapping tests ----

TEST_CASE("sr: remap_parse_result renames units", "[sr]") {
    ParseResult pr;
    DesignUnit u;
    u.name = "counter";
    u.kind = DesignUnitKind::Module;
    pr.units.push_back(std::move(u));

    SymbolMap mapping = {{"counter", "counter_abc12345"}};
    SymbolRemapper::remap_parse_result(pr, mapping);

    CHECK(pr.units[0].name == "counter_abc12345");
}

TEST_CASE("sr: remap_parse_result renames instantiations", "[sr]") {
    ParseResult pr;
    DesignUnit u;
    u.name = "top";
    u.kind = DesignUnitKind::Module;
    { Instantiation inst; inst.module_name = "counter"; inst.instance_name = "u_cnt"; inst.pos = {"top.v", 5, 1}; u.instantiations.push_back(std::move(inst)); }
    { Instantiation inst; inst.module_name = "fifo"; inst.instance_name = "u_fifo"; inst.pos = {"top.v", 10, 1}; u.instantiations.push_back(std::move(inst)); }
    pr.units.push_back(std::move(u));

    SymbolMap mapping = {{"counter", "counter_abc12345"}};
    SymbolRemapper::remap_parse_result(pr, mapping);

    CHECK(pr.units[0].name == "top");  // top is not in mapping
    CHECK(pr.units[0].instantiations[0].module_name == "counter_abc12345");
    CHECK(pr.units[0].instantiations[1].module_name == "fifo");  // not in mapping
}

TEST_CASE("sr: remap_parse_result renames imports", "[sr]") {
    ParseResult pr;
    DesignUnit u;
    u.name = "my_module";
    u.kind = DesignUnitKind::Module;
    u.imports.push_back({"common_pkg", "*", true, {"a.sv", 1, 1}});
    pr.units.push_back(std::move(u));

    SymbolMap mapping = {{"common_pkg", "common_pkg_12345678"}};
    SymbolRemapper::remap_parse_result(pr, mapping);

    CHECK(pr.units[0].imports[0].package_name == "common_pkg_12345678");
}

// ---- Report formatting tests ----

TEST_CASE("sr: format_report produces readable output", "[sr]") {
    std::vector<SymbolCollision> collisions = {{
        "counter",
        {{"ip_a/counter.v", DesignUnitKind::Module, 5},
         {"ip_b/counter.v", DesignUnitKind::Module, 10}}
    }};

    auto report = SymbolRemapper::format_report(collisions);
    CHECK(report.find("counter") != std::string::npos);
    CHECK(report.find("ip_a/counter.v:5") != std::string::npos);
    CHECK(report.find("ip_b/counter.v:10") != std::string::npos);
    CHECK(report.find("2 locations") != std::string::npos);
}

TEST_CASE("sr: format_report empty collisions", "[sr]") {
    auto report = SymbolRemapper::format_report({});
    CHECK(report.empty());
}

TEST_CASE("sr: format_json produces valid structure", "[sr]") {
    std::vector<SymbolCollision> collisions = {{
        "fifo",
        {{"a.v", DesignUnitKind::Module, 1},
         {"b.v", DesignUnitKind::Module, 1}}
    }};

    auto json = SymbolRemapper::format_json(collisions);
    CHECK(json.find("\"name\": \"fifo\"") != std::string::npos);
    CHECK(json.find("\"file\": \"a.v\"") != std::string::npos);
    CHECK(json.find("\"file\": \"b.v\"") != std::string::npos);
    CHECK(json.front() == '[');
}

TEST_CASE("sr: format_json empty collisions", "[sr]") {
    auto json = SymbolRemapper::format_json({});
    CHECK(json == "[\n]\n");
}

// ---- End-to-end: detect + remap pipeline ----

TEST_CASE("sr: full pipeline detect + remap", "[sr]") {
    // Two files both define "counter"
    std::unordered_map<std::string, ParseResult> prs;

    ParseResult pr_a;
    DesignUnit ua;
    ua.name = "counter";
    ua.kind = DesignUnitKind::Module;
    ua.start_line = 1;
    ua.depth = 0;
    pr_a.units.push_back(std::move(ua));
    prs["ip_a/counter.v"] = std::move(pr_a);

    ParseResult pr_b;
    DesignUnit ub;
    ub.name = "counter";
    ub.kind = DesignUnitKind::Module;
    ub.start_line = 1;
    ub.depth = 0;
    { Instantiation inst; inst.module_name = "counter"; inst.instance_name = "u_cnt"; inst.pos = {"ip_b/counter.v", 5, 1}; ub.instantiations.push_back(std::move(inst)); }
    pr_b.units.push_back(std::move(ub));
    prs["ip_b/counter.v"] = std::move(pr_b);

    // Step 1: detect
    auto collisions = SymbolRemapper::detect_collisions(prs);
    REQUIRE(collisions.size() == 1);
    CHECK(collisions[0].name == "counter");

    // Step 2: build remap table
    auto table = SymbolRemapper::build_remap_table(collisions);
    REQUIRE(table.size() == 2);

    // Step 3: apply to each file's parse result
    for (auto& [file, pr] : prs) {
        auto it = table.find(file);
        if (it != table.end()) {
            SymbolRemapper::remap_parse_result(pr, it->second);
        }
    }

    // Verify: each file's "counter" is now mangled differently
    auto& units_a = prs["ip_a/counter.v"].units;
    auto& units_b = prs["ip_b/counter.v"].units;
    REQUIRE(units_a.size() == 1);
    REQUIRE(units_b.size() == 1);
    CHECK(units_a[0].name != "counter");
    CHECK(units_b[0].name != "counter");
    CHECK(units_a[0].name != units_b[0].name);
    // Both start with "counter_"
    CHECK(units_a[0].name.substr(0, 8) == "counter_");
    CHECK(units_b[0].name.substr(0, 8) == "counter_");
}
