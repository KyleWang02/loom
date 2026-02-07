#include <catch2/catch.hpp>
#include <loom/lang/parser.hpp>
#include <fstream>
#include <sstream>

using namespace loom;

static std::string fixture_dir() {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures";
    return "../tests/fixtures";
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Helper: lex + parse in one call
static ParseResult lex_and_parse(const std::string& src,
                                  bool is_sv = false,
                                  const std::string& fname = "<test>") {
    auto lr = lex(src, fname, is_sv);
    REQUIRE(lr.is_ok());
    auto pr = parse(lr.value(), fname, is_sv);
    REQUIRE(pr.is_ok());
    return std::move(pr.value());
}

// ===== Chunk 1: Design Unit Extraction =====

TEST_CASE("parse empty string", "[parser]") {
    auto r = lex_and_parse("");
    REQUIRE(r.units.empty());
    REQUIRE(r.diagnostics.empty());
}

TEST_CASE("parse file with only comments", "[parser]") {
    auto r = lex_and_parse("// just a comment\n/* block */\n");
    REQUIRE(r.units.empty());
}

TEST_CASE("parse single module", "[parser]") {
    auto r = lex_and_parse("module foo; endmodule");
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "foo");
    CHECK(r.units[0].kind == DesignUnitKind::Module);
    CHECK(r.units[0].depth == 0);
}

TEST_CASE("parse module with line tracking", "[parser]") {
    auto r = lex_and_parse("module foo;\n\nendmodule\n");
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].start_line == 1);
    CHECK(r.units[0].end_line == 3);
}

TEST_CASE("parse multiple modules", "[parser]") {
    auto r = lex_and_parse(
        "module a; endmodule\n"
        "module b; endmodule\n"
        "module c; endmodule\n"
    );
    REQUIRE(r.units.size() == 3);
    CHECK(r.units[0].name == "a");
    CHECK(r.units[1].name == "b");
    CHECK(r.units[2].name == "c");
}

TEST_CASE("parse package", "[parser]") {
    auto r = lex_and_parse("package my_pkg; endpackage", true);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "my_pkg");
    CHECK(r.units[0].kind == DesignUnitKind::Package);
}

TEST_CASE("parse interface", "[parser]") {
    auto r = lex_and_parse("interface axi_if; endinterface", true);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "axi_if");
    CHECK(r.units[0].kind == DesignUnitKind::Interface);
}

TEST_CASE("parse class", "[parser]") {
    auto r = lex_and_parse("class my_class; endclass", true);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "my_class");
    CHECK(r.units[0].kind == DesignUnitKind::Class);
}

TEST_CASE("parse program", "[parser]") {
    auto r = lex_and_parse("program my_prog; endprogram", true);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "my_prog");
    CHECK(r.units[0].kind == DesignUnitKind::Program);
}

TEST_CASE("parse class with extends", "[parser]") {
    auto r = lex_and_parse("class child extends parent; endclass", true);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "child");
    CHECK(r.units[0].kind == DesignUnitKind::Class);
}

TEST_CASE("parse nested modules", "[parser]") {
    auto r = lex_and_parse(
        "module outer;\n"
        "  module inner;\n"
        "  endmodule\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 2);
    CHECK(r.units[0].name == "inner");
    CHECK(r.units[0].depth == 1);
    CHECK(r.units[1].name == "outer");
    CHECK(r.units[1].depth == 0);
}

TEST_CASE("parse module with automatic", "[parser]") {
    auto r = lex_and_parse("module automatic foo; endmodule");
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "foo");
}

TEST_CASE("error recovery on malformed input", "[parser]") {
    // Missing module name — should recover and still parse second module
    auto r = lex_and_parse("module ; endmodule\nmodule good; endmodule\n");
    // Parser should produce at least the good module
    bool found_good = false;
    for (auto& u : r.units) {
        if (u.name == "good") found_good = true;
    }
    CHECK(found_good);
    CHECK(!r.diagnostics.empty());
}

TEST_CASE("parse endmodule with colon name", "[parser]") {
    auto r = lex_and_parse("module foo; endmodule : foo");
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "foo");
}

TEST_CASE("parse mixed design units", "[parser]") {
    auto r = lex_and_parse(
        "package pkg; endpackage\n"
        "interface intf; endinterface\n"
        "module mod; endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 3);
    CHECK(r.units[0].kind == DesignUnitKind::Package);
    CHECK(r.units[1].kind == DesignUnitKind::Interface);
    CHECK(r.units[2].kind == DesignUnitKind::Module);
}

// ===== Chunk 2: Port and Parameter Extraction =====

TEST_CASE("parse ANSI ports", "[parser]") {
    auto r = lex_and_parse(
        "module foo(\n"
        "    input wire clk,\n"
        "    input wire rst,\n"
        "    output reg [7:0] data\n"
        ");\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& ports = r.units[0].ports;
    REQUIRE(ports.size() == 3);
    CHECK(ports[0].name == "clk");
    CHECK(ports[0].direction == PortDirection::Input);
    CHECK(ports[1].name == "rst");
    CHECK(ports[1].direction == PortDirection::Input);
    CHECK(ports[2].name == "data");
    CHECK(ports[2].direction == PortDirection::Output);
}

TEST_CASE("parse ANSI parameters", "[parser]") {
    auto r = lex_and_parse(
        "module foo #(\n"
        "    parameter WIDTH = 8,\n"
        "    parameter DEPTH = 16\n"
        ")(\n"
        "    input wire clk\n"
        ");\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& params = r.units[0].params;
    REQUIRE(params.size() == 2);
    CHECK(params[0].name == "WIDTH");
    CHECK(params[0].default_text == "8");
    CHECK(params[1].name == "DEPTH");
    CHECK(params[1].default_text == "16");
}

TEST_CASE("parse non-ANSI ports", "[parser]") {
    auto r = lex_and_parse(
        "module foo(a, b, c);\n"
        "    input wire a;\n"
        "    input wire b;\n"
        "    output reg c;\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& ports = r.units[0].ports;
    REQUIRE(ports.size() == 3);
    CHECK(ports[0].name == "a");
    CHECK(ports[0].direction == PortDirection::Input);
    CHECK(ports[1].name == "b");
    CHECK(ports[1].direction == PortDirection::Input);
    CHECK(ports[2].name == "c");
    CHECK(ports[2].direction == PortDirection::Output);
}

TEST_CASE("parse module with no ports", "[parser]") {
    auto r = lex_and_parse("module foo; endmodule");
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].ports.empty());
    CHECK(r.units[0].params.empty());
}

TEST_CASE("parse ports with SV types", "[parser]") {
    auto r = lex_and_parse(
        "module foo(\n"
        "    input  logic        clk,\n"
        "    input  logic        rst_n,\n"
        "    output logic [31:0] data\n"
        ");\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& ports = r.units[0].ports;
    REQUIRE(ports.size() == 3);
    CHECK(ports[0].name == "clk");
    CHECK(ports[2].name == "data");
    CHECK(ports[2].direction == PortDirection::Output);
}

TEST_CASE("parse body localparam", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    localparam MAX = 255;\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& params = r.units[0].params;
    REQUIRE(params.size() == 1);
    CHECK(params[0].name == "MAX");
    CHECK(params[0].is_localparam == true);
    CHECK(params[0].default_text == "255");
}

TEST_CASE("parse counter.v fixture", "[parser][fixture]") {
    auto src = read_file(fixture_dir() + "/counter.v");
    REQUIRE(!src.empty());
    auto r = lex_and_parse(src);
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "counter");
    CHECK(r.units[0].params.size() == 1);
    CHECK(r.units[0].params[0].name == "WIDTH");
    CHECK(r.units[0].ports.size() == 4); // clk, rst, en, count
}

// ===== Chunk 3: Instantiation Detection + Imports =====

TEST_CASE("parse simple instantiation", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    foo u_foo(.a(x), .b(y));\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& insts = r.units[0].instantiations;
    REQUIRE(insts.size() == 1);
    CHECK(insts[0].module_name == "foo");
    CHECK(insts[0].instance_name == "u_foo");
    CHECK(insts[0].is_parameterized == false);
}

TEST_CASE("parse parameterized instantiation", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    counter #(.WIDTH(16)) u_cnt(.clk(clk));\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& insts = r.units[0].instantiations;
    REQUIRE(insts.size() == 1);
    CHECK(insts[0].module_name == "counter");
    CHECK(insts[0].instance_name == "u_cnt");
    CHECK(insts[0].is_parameterized == true);
}

TEST_CASE("parse multiple instantiations", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    foo u_a(.x(1));\n"
        "    bar u_b(.y(2));\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& insts = r.units[0].instantiations;
    REQUIRE(insts.size() == 2);
    CHECK(insts[0].module_name == "foo");
    CHECK(insts[1].module_name == "bar");
}

TEST_CASE("parse import wildcard", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    import pkg::*;\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& imps = r.units[0].imports;
    REQUIRE(imps.size() == 1);
    CHECK(imps[0].package_name == "pkg");
    CHECK(imps[0].is_wildcard == true);
    CHECK(imps[0].symbol == "*");
}

TEST_CASE("parse import specific symbol", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    import pkg::my_type;\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& imps = r.units[0].imports;
    REQUIRE(imps.size() == 1);
    CHECK(imps[0].package_name == "pkg");
    CHECK(imps[0].symbol == "my_type");
    CHECK(imps[0].is_wildcard == false);
}

TEST_CASE("parse package_example.sv fixture", "[parser][fixture]") {
    auto src = read_file(fixture_dir() + "/package_example.sv");
    REQUIRE(!src.empty());
    auto r = lex_and_parse(src, true);

    // Should have: common_types (package), axi_if (interface), top_module (module)
    REQUIRE(r.units.size() == 3);

    CHECK(r.units[0].name == "common_types");
    CHECK(r.units[0].kind == DesignUnitKind::Package);

    CHECK(r.units[1].name == "axi_if");
    CHECK(r.units[1].kind == DesignUnitKind::Interface);
    CHECK(r.units[1].params.size() == 2); // ADDR_W, DATA_W

    CHECK(r.units[2].name == "top_module");
    CHECK(r.units[2].kind == DesignUnitKind::Module);
    CHECK(r.units[2].imports.size() >= 1);
    CHECK(r.units[2].imports[0].package_name == "common_types");
}

// ===== Chunk 4: Lint-Relevant Structures =====

TEST_CASE("parse always_comb block", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        x = 1;\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].kind == AlwaysKind::Comb);
    REQUIRE(ab[0].assignments.size() == 1);
    CHECK(ab[0].assignments[0].is_blocking == true);
    CHECK(ab[0].assignments[0].target == "x");
}

TEST_CASE("parse always_ff block", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_ff @(posedge clk) begin\n"
        "        q <= d;\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].kind == AlwaysKind::Ff);
    REQUIRE(ab[0].assignments.size() == 1);
    CHECK(ab[0].assignments[0].is_blocking == false);
    CHECK(ab[0].assignments[0].target == "q");
}

TEST_CASE("parse always_latch block", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_latch begin\n"
        "        if (en) q <= d;\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].kind == AlwaysKind::Latch);
}

TEST_CASE("parse always @(*) block", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always @(*) begin\n"
        "        y = a + b;\n"
        "    end\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].kind == AlwaysKind::Star);
}

TEST_CASE("parse always @(posedge clk) block", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always @(posedge clk) begin\n"
        "        q <= d;\n"
        "    end\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].kind == AlwaysKind::Plain);
}

TEST_CASE("parse always block with label", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_ff @(posedge clk) begin : my_ff\n"
        "        q <= d;\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& ab = r.units[0].always_blocks;
    REQUIRE(ab.size() == 1);
    CHECK(ab[0].label == "my_ff");
}

TEST_CASE("parse case statement with default", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        case (sel)\n"
        "            2'b00: y = a;\n"
        "            2'b01: y = b;\n"
        "            default: y = 0;\n"
        "        endcase\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& cs = r.units[0].case_statements;
    REQUIRE(cs.size() == 1);
    CHECK(cs[0].kind == CaseKind::Case);
    CHECK(cs[0].has_default == true);
}

TEST_CASE("parse case without default", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        case (sel)\n"
        "            2'b00: y = a;\n"
        "            2'b01: y = b;\n"
        "        endcase\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& cs = r.units[0].case_statements;
    REQUIRE(cs.size() == 1);
    CHECK(cs[0].has_default == false);
}

TEST_CASE("parse unique case", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        unique case (sel)\n"
        "            2'b00: y = a;\n"
        "            default: y = 0;\n"
        "        endcase\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& cs = r.units[0].case_statements;
    REQUIRE(cs.size() == 1);
    CHECK(cs[0].is_unique == true);
    CHECK(cs[0].has_default == true);
}

TEST_CASE("parse priority case", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        priority case (sel)\n"
        "            2'b00: y = a;\n"
        "            default: y = 0;\n"
        "        endcase\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& cs = r.units[0].case_statements;
    REQUIRE(cs.size() == 1);
    CHECK(cs[0].is_priority == true);
}

TEST_CASE("parse signal declarations", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    wire a;\n"
        "    reg [7:0] b;\n"
        "    logic [31:0] c;\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& sigs = r.units[0].signals;
    REQUIRE(sigs.size() == 3);
    CHECK(sigs[0].name == "a");
    CHECK(sigs[1].name == "b");
    CHECK(sigs[2].name == "c");
}

TEST_CASE("parse generate block with label", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    generate\n"
        "        for (genvar i = 0; i < 4; i = i + 1) begin : gen_loop\n"
        "        end\n"
        "    endgenerate\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& gb = r.units[0].generate_blocks;
    REQUIRE(gb.size() == 1);
    CHECK(gb[0].has_label == true);
    CHECK(gb[0].label == "gen_loop");
}

TEST_CASE("parse generate block without label", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    generate\n"
        "        for (genvar i = 0; i < 4; i = i + 1) begin\n"
        "        end\n"
        "    endgenerate\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    auto& gb = r.units[0].generate_blocks;
    REQUIRE(gb.size() == 1);
    CHECK(gb[0].has_label == false);
}

TEST_CASE("parse defparam detection", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    defparam u_bar.WIDTH = 16;\n"
        "endmodule\n"
    );
    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].has_defparam == true);
}

// ===== Chunk 5: Integration =====

TEST_CASE("parse counter.v full pipeline", "[parser][integration]") {
    auto src = read_file(fixture_dir() + "/counter.v");
    REQUIRE(!src.empty());

    auto lr = lex(src, "counter.v", false);
    REQUIRE(lr.is_ok());

    auto pr = parse(lr.value(), "counter.v", false);
    REQUIRE(pr.is_ok());

    auto& result = pr.value();
    REQUIRE(result.units.size() == 1);

    auto& m = result.units[0];
    CHECK(m.name == "counter");
    CHECK(m.kind == DesignUnitKind::Module);
    CHECK(m.params.size() == 1);
    CHECK(m.params[0].name == "WIDTH");
    CHECK(m.params[0].default_text == "8");
    CHECK(m.ports.size() == 4);
    CHECK(m.always_blocks.size() == 1);
    CHECK(m.always_blocks[0].kind == AlwaysKind::Plain);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("parse package_example.sv full pipeline", "[parser][integration]") {
    auto src = read_file(fixture_dir() + "/package_example.sv");
    REQUIRE(!src.empty());

    auto lr = lex(src, "package_example.sv", true);
    REQUIRE(lr.is_ok());

    auto pr = parse(lr.value(), "package_example.sv", true);
    REQUIRE(pr.is_ok());

    auto& result = pr.value();
    REQUIRE(result.units.size() == 3);

    // Package
    CHECK(result.units[0].name == "common_types");
    CHECK(result.units[0].kind == DesignUnitKind::Package);

    // Interface
    CHECK(result.units[1].name == "axi_if");
    CHECK(result.units[1].kind == DesignUnitKind::Interface);
    CHECK(result.units[1].params.size() == 2);

    // Module
    CHECK(result.units[2].name == "top_module");
    CHECK(result.units[2].kind == DesignUnitKind::Module);
    CHECK(result.units[2].always_blocks.size() == 2);

    CHECK(result.diagnostics.empty());
}

TEST_CASE("parse casex and casez", "[parser]") {
    auto r = lex_and_parse(
        "module foo;\n"
        "    always_comb begin\n"
        "        casex (sel)\n"
        "            default: y = 0;\n"
        "        endcase\n"
        "        casez (sel)\n"
        "            default: y = 0;\n"
        "        endcase\n"
        "    end\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& cs = r.units[0].case_statements;
    REQUIRE(cs.size() == 2);
    CHECK(cs[0].kind == CaseKind::Casex);
    CHECK(cs[1].kind == CaseKind::Casez);
}

TEST_CASE("parse nested_module.sv fixture", "[parser][fixture]") {
    auto src = read_file(fixture_dir() + "/nested_module.sv");
    REQUIRE(!src.empty());
    auto r = lex_and_parse(src, true);

    REQUIRE(r.units.size() == 2);
    // Inner module parsed first (it's completed first inside parse_body)
    CHECK(r.units[0].name == "inner");
    CHECK(r.units[0].depth == 1);
    CHECK(r.units[0].ports.size() == 2);

    CHECK(r.units[1].name == "outer");
    CHECK(r.units[1].depth == 0);
    CHECK(r.units[1].params.size() == 1);
    CHECK(r.units[1].ports.size() == 3);
    CHECK(r.units[1].instantiations.size() == 1);
    CHECK(r.units[1].instantiations[0].module_name == "inner");
}

TEST_CASE("parse non_ansi_module.v fixture", "[parser][fixture]") {
    auto src = read_file(fixture_dir() + "/non_ansi_module.v");
    REQUIRE(!src.empty());
    auto r = lex_and_parse(src);

    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].name == "legacy_adder");
    CHECK(r.units[0].params.size() == 1);
    CHECK(r.units[0].params[0].name == "WIDTH");

    auto& ports = r.units[0].ports;
    REQUIRE(ports.size() == 5);
    // Non-ANSI ports get direction from body declarations
    CHECK(ports[0].name == "a");
    CHECK(ports[0].direction == PortDirection::Input);
    CHECK(ports[4].name == "cout");
    CHECK(ports[4].direction == PortDirection::Output);
}

TEST_CASE("parse lint_constructs.sv fixture", "[parser][fixture]") {
    auto src = read_file(fixture_dir() + "/lint_constructs.sv");
    REQUIRE(!src.empty());
    auto r = lex_and_parse(src, true);

    REQUIRE(r.units.size() == 1);
    auto& u = r.units[0];
    CHECK(u.name == "lint_test");

    // Always blocks: comb, ff, latch + 2 more (unique case, priority case)
    CHECK(u.always_blocks.size() >= 3);

    // Verify always kinds
    bool found_comb = false, found_ff = false, found_latch = false;
    for (auto& ab : u.always_blocks) {
        if (ab.kind == AlwaysKind::Comb) found_comb = true;
        if (ab.kind == AlwaysKind::Ff) found_ff = true;
        if (ab.kind == AlwaysKind::Latch) found_latch = true;
    }
    CHECK(found_comb);
    CHECK(found_ff);
    CHECK(found_latch);

    // Case statements (at least 3: regular, unique, priority)
    CHECK(u.case_statements.size() >= 3);

    // Generate blocks (labeled + unlabeled)
    CHECK(u.generate_blocks.size() >= 2);
    bool found_labeled = false, found_unlabeled = false;
    for (auto& gb : u.generate_blocks) {
        if (gb.has_label) found_labeled = true;
        else found_unlabeled = true;
    }
    CHECK(found_labeled);
    CHECK(found_unlabeled);

    // Defparam
    CHECK(u.has_defparam == true);
}

TEST_CASE("parse multiple imports", "[parser]") {
    auto r = lex_and_parse(
        "module top;\n"
        "    import pkg_a::*;\n"
        "    import pkg_b::func_b;\n"
        "endmodule\n",
        true
    );
    REQUIRE(r.units.size() == 1);
    auto& imps = r.units[0].imports;
    REQUIRE(imps.size() == 2);
    CHECK(imps[0].package_name == "pkg_a");
    CHECK(imps[0].is_wildcard == true);
    CHECK(imps[1].package_name == "pkg_b");
    CHECK(imps[1].symbol == "func_b");
}
