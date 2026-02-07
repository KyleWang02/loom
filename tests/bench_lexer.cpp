#include <catch2/catch.hpp>
#include <loom/lang/lexer.hpp>
#include <chrono>
#include <sstream>

using namespace loom;

// Generate a synthetic Verilog file with N modules, ~50 lines each
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
        out << "    // loom: ignore[blocking-in-ff]\n";
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
        out << "    `ifdef SIMULATION\n";
        out << "    initial begin\n";
        out << "        $display(\"Module mod_" << m << " initialized\");\n";
        out << "    end\n";
        out << "    `endif\n\n";
        out << "endmodule\n\n";
    }
    return out.str();
}

TEST_CASE("lexer perf: 10K-line Verilog file under 100ms", "[lexer][bench]") {
    // ~220 modules * ~46 lines each = ~10,120 lines
    auto source = generate_verilog(220);

    // Count actual lines
    int line_count = 0;
    for (char c : source) {
        if (c == '\n') ++line_count;
    }
    REQUIRE(line_count >= 10000);

    // Time the lexer
    auto start = std::chrono::high_resolution_clock::now();
    auto r = lex(source, "bench.v");
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(r.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Report
    auto& tokens = r.value().tokens;
    auto& comments = r.value().comments;
    INFO("Lines: " << line_count);
    INFO("Tokens: " << tokens.size());
    INFO("Comments: " << comments.size());
    INFO("Time: " << ms << " ms");

    REQUIRE(ms < 100);
}

TEST_CASE("lexer perf: 50K-line SV file under 500ms", "[lexer][bench]") {
    // ~1000 modules * ~50 lines each = ~50,000 lines
    auto source = generate_verilog(1000);

    int line_count = 0;
    for (char c : source) {
        if (c == '\n') ++line_count;
    }
    REQUIRE(line_count >= 40000);

    auto start = std::chrono::high_resolution_clock::now();
    auto r = lex(source, "bench_large.sv", true);
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(r.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Lines: " << line_count);
    INFO("Tokens: " << r.value().tokens.size());
    INFO("Time: " << ms << " ms");

    REQUIRE(ms < 500);
}
