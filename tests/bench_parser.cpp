#include <catch2/catch.hpp>
#include <loom/lang/parser.hpp>
#include <chrono>
#include <sstream>

using namespace loom;

// Generate a synthetic Verilog file with N modules, ~46 lines each
static std::string generate_verilog(int num_modules) {
    std::ostringstream out;
    for (int m = 0; m < num_modules; ++m) {
        out << "/// Module " << m << " documentation.\n";
        out << "module mod_" << m << " #(\n";
        out << "    parameter WIDTH = 8,\n";
        out << "    parameter DEPTH = 16\n";
        out << ")(\n";
        out << "    input  wire             clk,\n";
        out << "    input  wire             rst,\n";
        out << "    input  wire             en,\n";
        out << "    input  wire [WIDTH-1:0] data_in,\n";
        out << "    output reg  [WIDTH-1:0] data_out,\n";
        out << "    output wire             valid\n";
        out << ");\n\n";
        out << "    localparam ADDR_W = $clog2(DEPTH);\n";
        out << "    reg [WIDTH-1:0] mem [0:DEPTH-1];\n";
        out << "    reg [ADDR_W-1:0] wr_ptr;\n";
        out << "    reg [ADDR_W-1:0] rd_ptr;\n\n";
        out << "    assign valid = (wr_ptr != rd_ptr);\n\n";
        out << "    always @(posedge clk or posedge rst) begin\n";
        out << "        if (rst) begin\n";
        out << "            wr_ptr <= {ADDR_W{1'b0}};\n";
        out << "            rd_ptr <= {ADDR_W{1'b0}};\n";
        out << "            data_out <= {WIDTH{1'b0}};\n";
        out << "        end else begin\n";
        out << "            if (en) begin\n";
        out << "                mem[wr_ptr] <= data_in;\n";
        out << "                wr_ptr <= wr_ptr + 1;\n";
        out << "            end\n";
        out << "            if (valid) begin\n";
        out << "                data_out <= mem[rd_ptr];\n";
        out << "                rd_ptr <= rd_ptr + 1;\n";
        out << "            end\n";
        out << "        end\n";
        out << "    end\n\n";
        // Add an instantiation for dependency tracking test
        if (m > 0) {
            out << "    mod_" << (m-1) << " u_sub(.clk(clk), .rst(rst));\n\n";
        }
        out << "endmodule\n\n";
    }
    return out.str();
}

TEST_CASE("parser perf: 10K lines under 50ms", "[parser][bench]") {
    // ~245 modules * ~41 lines each ≈ 10,045 lines
    auto source = generate_verilog(245);

    int line_count = 0;
    for (char c : source) {
        if (c == '\n') ++line_count;
    }
    REQUIRE(line_count >= 10000);

    // Lex first
    auto lr = lex(source, "bench.v");
    REQUIRE(lr.is_ok());

    // Time the parser only
    auto start = std::chrono::high_resolution_clock::now();
    auto pr = parse(lr.value(), "bench.v");
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(pr.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Lines: " << line_count);
    INFO("Modules: " << pr.value().units.size());
    INFO("Time: " << ms << " ms");

    CHECK(pr.value().units.size() == 245);
    REQUIRE(ms < 50);
}

TEST_CASE("parser perf: 50K lines under 200ms", "[parser][bench]") {
    // ~1220 modules * ~41 lines each ≈ 50,020 lines
    auto source = generate_verilog(1220);

    int line_count = 0;
    for (char c : source) {
        if (c == '\n') ++line_count;
    }
    REQUIRE(line_count >= 50000);

    auto lr = lex(source, "bench_large.v");
    REQUIRE(lr.is_ok());

    auto start = std::chrono::high_resolution_clock::now();
    auto pr = parse(lr.value(), "bench_large.v");
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(pr.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Lines: " << line_count);
    INFO("Modules: " << pr.value().units.size());
    INFO("Time: " << ms << " ms");

    CHECK(pr.value().units.size() == 1220);
    REQUIRE(ms < 200);
}

TEST_CASE("parser perf: extraction correctness at scale", "[parser][bench]") {
    auto source = generate_verilog(100);

    auto lr = lex(source, "bench_check.v");
    REQUIRE(lr.is_ok());

    auto pr = parse(lr.value(), "bench_check.v");
    REQUIRE(pr.is_ok());

    auto& units = pr.value().units;
    REQUIRE(units.size() == 100);

    // Verify each module has expected structure
    for (size_t i = 0; i < units.size(); ++i) {
        auto& u = units[i];
        CHECK(u.name == "mod_" + std::to_string(i));
        CHECK(u.kind == DesignUnitKind::Module);
        CHECK(u.params.size() == 3); // WIDTH, DEPTH, ADDR_W (body localparam)
        CHECK(u.ports.size() == 6);  // clk, rst, en, data_in, data_out, valid
        CHECK(u.always_blocks.size() == 1);
        if (i > 0) {
            CHECK(u.instantiations.size() == 1);
        }
    }

    CHECK(pr.value().diagnostics.empty());
}
