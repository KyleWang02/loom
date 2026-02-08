// demo_filelist.cpp — Filelist Generation demonstration
//
// Exercises FilelistGenerator with real Verilog/SystemVerilog files:
//   1. Simple single-module filelist
//   2. Linear dependency chain (providers-first topological order)
//   3. Diamond dependency (shared leaf module)
//   4. Top module auto-detection
//   5. Black box detection (instantiated but undefined)
//   6. Testbench detection heuristics
//   7. .f file output format (EDA tool compatible)
//   8. JSON output format
//   9. Multi-module files (two modules in one file)
//  10. Package + interface support
//
// All operations use temporary directories (auto-cleaned).

#include <loom/filelist.hpp>
#include <loom/target_expr.hpp>
#include <loom/log.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace loom;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void print_header(const char* title) {
    std::cout << "\n"
              << "========================================================\n"
              << "  " << title << "\n"
              << "========================================================\n\n";
}

struct TempDir {
    fs::path path;
    TempDir(const std::string& suffix) {
        path = fs::temp_directory_path() / ("loom_demo_fl_" + suffix + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p.string();
    }
};

static void print_result(const FilelistResult& r) {
    std::cout << "  Files (providers-first order):\n";
    for (size_t i = 0; i < r.files.size(); ++i) {
        std::cout << "    " << (i + 1) << ". " << fs::path(r.files[i].file_path).filename().string();
        if (!r.files[i].defines.empty()) {
            std::cout << "  defines=[";
            for (size_t j = 0; j < r.files[i].defines.size(); ++j) {
                if (j > 0) std::cout << ", ";
                std::cout << r.files[i].defines[j];
            }
            std::cout << "]";
        }
        if (!r.files[i].include_dirs.empty()) {
            std::cout << "  incdirs=[";
            for (size_t j = 0; j < r.files[i].include_dirs.size(); ++j) {
                if (j > 0) std::cout << ", ";
                std::cout << fs::path(r.files[i].include_dirs[j]).filename().string();
            }
            std::cout << "]";
        }
        std::cout << "\n";
    }
    std::cout << "\n  Top modules:      [";
    for (size_t i = 0; i < r.top_modules.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << r.top_modules[i];
    }
    std::cout << "]\n";

    std::cout << "  Testbench modules: [";
    for (size_t i = 0; i < r.testbench_modules.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << r.testbench_modules[i];
    }
    std::cout << "]\n";

    std::cout << "  Black boxes:      [";
    for (size_t i = 0; i < r.black_boxes.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << r.black_boxes[i];
    }
    std::cout << "]\n";
}

// ===========================================================================
// Demo scenarios
// ===========================================================================

static void demo_single_module() {
    print_header("1. SINGLE MODULE FILELIST");

    TempDir tmp("single");
    auto f = tmp.write("counter.v",
        "module counter(input clk, input rst, output reg [7:0] count);\n"
        "  always @(posedge clk) begin\n"
        "    if (rst) count <= 0;\n"
        "    else count <= count + 1;\n"
        "  end\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Source: counter.v (single standalone module)\n\n";
    print_result(r.value());
}

static void demo_linear_chain() {
    print_header("2. LINEAR DEPENDENCY CHAIN (A -> B -> C)");

    TempDir tmp("chain");
    auto f_top = tmp.write("top.v",
        "module top(input clk);\n"
        "  mid mid_inst(.clk(clk));\n"
        "endmodule\n");
    auto f_mid = tmp.write("mid.v",
        "module mid(input clk);\n"
        "  leaf leaf_inst(.clk(clk));\n"
        "endmodule\n");
    auto f_leaf = tmp.write("leaf.v",
        "module leaf(input clk);\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_top, f_mid, f_leaf};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Dependency chain: top -> mid -> leaf\n";
    std::cout << "Expected order: leaf, mid, top (providers first for EDA tools)\n\n";
    print_result(r.value());

    // Verify providers-first
    auto& files = r.value().files;
    bool correct_order = true;
    int leaf_idx = -1, mid_idx = -1, top_idx = -1;
    for (int i = 0; i < (int)files.size(); ++i) {
        auto name = fs::path(files[i].file_path).filename().string();
        if (name == "leaf.v") leaf_idx = i;
        if (name == "mid.v") mid_idx = i;
        if (name == "top.v") top_idx = i;
    }
    correct_order = (leaf_idx < mid_idx && mid_idx < top_idx);
    std::cout << "\n  Providers-first order correct: " << (correct_order ? "YES" : "NO") << "\n";
}

static void demo_diamond_deps() {
    print_header("3. DIAMOND DEPENDENCY (shared leaf)");

    TempDir tmp("diamond");
    auto f_top = tmp.write("top.v",
        "module top(input clk);\n"
        "  branch_a a1(.clk(clk));\n"
        "  branch_b b1(.clk(clk));\n"
        "endmodule\n");
    auto f_a = tmp.write("branch_a.v",
        "module branch_a(input clk);\n"
        "  shared shared_inst(.clk(clk));\n"
        "endmodule\n");
    auto f_b = tmp.write("branch_b.v",
        "module branch_b(input clk);\n"
        "  shared shared_inst(.clk(clk));\n"
        "endmodule\n");
    auto f_shared = tmp.write("shared.v",
        "module shared(input clk);\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_top, f_a, f_b, f_shared};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Diamond graph:\n";
    std::cout << "  top -> branch_a -> shared\n";
    std::cout << "  top -> branch_b -> shared\n\n";
    print_result(r.value());

    std::cout << "\n  shared.v appears once (no duplicates): "
              << (r.value().files.size() == 4 ? "YES" : "NO") << "\n";
}

static void demo_top_module_detection() {
    print_header("4. TOP MODULE AUTO-DETECTION");

    TempDir tmp("topmod");
    auto f1 = tmp.write("soc.v",
        "module soc(input clk);\n"
        "  cpu cpu_inst(.clk(clk));\n"
        "  mem mem_inst(.clk(clk));\n"
        "endmodule\n");
    auto f2 = tmp.write("cpu.v",
        "module cpu(input clk);\n"
        "endmodule\n");
    auto f3 = tmp.write("mem.v",
        "module mem(input clk);\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f1, f2, f3};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Modules: soc (instantiates cpu, mem), cpu, mem\n";
    std::cout << "Expected top: soc (in-degree 0 = nobody instantiates it)\n\n";
    print_result(r.value());
}

static void demo_black_boxes() {
    print_header("5. BLACK BOX DETECTION (instantiated but undefined)");

    TempDir tmp("bbox");
    auto f = tmp.write("top_with_bbox.v",
        "module top_with_bbox(input clk);\n"
        "  unknown_ip u1(.clk(clk));\n"
        "  mystery_ram u2(.clk(clk));\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Module: top_with_bbox instantiates unknown_ip and mystery_ram\n";
    std::cout << "Neither is defined in the source set.\n\n";
    print_result(r.value());

    std::cout << "\n  Black boxes detected: " << r.value().black_boxes.size() << "\n";
}

static void demo_testbench_detection() {
    print_header("6. TESTBENCH DETECTION HEURISTICS");

    TempDir tmp("tb");
    auto f_dut = tmp.write("dut.v",
        "module dut(input clk, input [7:0] data, output [7:0] result);\n"
        "  assign result = data;\n"
        "endmodule\n");
    auto f_tb = tmp.write("tb/tb_dut.v",
        "module tb_dut;\n"
        "  reg clk;\n"
        "  reg [7:0] data;\n"
        "  wire [7:0] result;\n"
        "  dut dut_inst(.clk(clk), .data(data), .result(result));\n"
        "  initial begin\n"
        "    clk = 0;\n"
        "    forever #5 clk = ~clk;\n"
        "  end\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_dut, f_tb};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;
    auto r = gen.generate_from_groups({g}, opts);
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Modules: dut (has ports), tb_dut (no ports, in tb/ dir, name has 'tb')\n";
    std::cout << "Heuristics: name contains 'tb' + in tb/ directory + no ports\n\n";
    print_result(r.value());
}

static void demo_dot_f_output() {
    print_header("7. .f FILE OUTPUT (EDA tool compatible)");

    TempDir tmp("dotf");
    auto f1 = tmp.write("pkg.sv",
        "package my_pkg;\n"
        "  typedef logic [7:0] byte_t;\n"
        "endpackage\n");
    auto f2 = tmp.write("dut.sv",
        "module dut;\n"
        "  import my_pkg::*;\n"
        "  byte_t data;\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f1, f2};
    g.defines = {"SYNTHESIS", "WIDTH=8"};
    g.include_dirs = {tmp.path.string()};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Source group with defines=[SYNTHESIS, WIDTH=8] and include dir\n\n";
    std::cout << ".f file content:\n";
    std::cout << "  ----------------------------------------\n";
    auto dot_f = r.value().to_dot_f();
    // Indent each line
    std::istringstream iss(dot_f);
    std::string line;
    while (std::getline(iss, line)) {
        std::cout << "  " << line << "\n";
    }
    std::cout << "  ----------------------------------------\n";
}

static void demo_json_output() {
    print_header("8. JSON OUTPUT");

    TempDir tmp("json");
    auto f = tmp.write("simple.v",
        "module simple(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "JSON output:\n";
    std::cout << "  ----------------------------------------\n";
    auto json = r.value().to_json();
    std::istringstream iss(json);
    std::string line;
    while (std::getline(iss, line)) {
        std::cout << "  " << line << "\n";
    }
    std::cout << "  ----------------------------------------\n";
}

static void demo_multi_module_file() {
    print_header("9. MULTI-MODULE FILE (two modules in one file)");

    TempDir tmp("multi");
    auto f_both = tmp.write("both.v",
        "module encoder(input [3:0] in, output [1:0] out);\n"
        "  assign out = in[1:0];\n"
        "endmodule\n"
        "\n"
        "module decoder(input [1:0] in, output [3:0] out);\n"
        "  assign out = {2'b0, in};\n"
        "endmodule\n");
    auto f_top = tmp.write("codec.v",
        "module codec(input [3:0] data_in, output [3:0] data_out);\n"
        "  wire [1:0] encoded;\n"
        "  encoder enc(.in(data_in), .out(encoded));\n"
        "  decoder dec(.in(encoded), .out(data_out));\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_both, f_top};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "both.v contains: encoder, decoder\n";
    std::cout << "codec.v contains: codec (instantiates encoder + decoder)\n";
    std::cout << "Expected: both.v before codec.v (providers first)\n\n";
    print_result(r.value());
}

static void demo_pkg_and_interface() {
    print_header("10. PACKAGE + INTERFACE SUPPORT");

    TempDir tmp("pkgif");
    auto f_pkg = tmp.write("bus_pkg.sv",
        "package bus_pkg;\n"
        "  typedef logic [31:0] addr_t;\n"
        "  typedef logic [63:0] data_t;\n"
        "endpackage\n");
    auto f_if = tmp.write("bus_if.sv",
        "interface bus_if;\n"
        "  logic req;\n"
        "  logic gnt;\n"
        "  modport master(output req, input gnt);\n"
        "  modport slave(input req, output gnt);\n"
        "endinterface\n");
    auto f_top = tmp.write("bus_master.sv",
        "module bus_master;\n"
        "  import bus_pkg::*;\n"
        "  bus_if bus();\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_pkg, f_if, f_top};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    if (r.is_err()) { std::cerr << "ERROR: " << r.error().format() << "\n"; return; }

    std::cout << "Design:\n";
    std::cout << "  bus_pkg    — package with types\n";
    std::cout << "  bus_if     — interface with modports\n";
    std::cout << "  bus_master — module importing pkg, instantiating interface\n\n";
    print_result(r.value());

    std::cout << "\n  Top module should be bus_master (packages/interfaces excluded from top)\n";
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
    log::set_level(log::Warn);  // suppress info noise

    std::cout << "============================================================\n";
    std::cout << "     Loom Filelist Generator — Comprehensive Demo\n";
    std::cout << "     Topological sort, detection, .f and JSON output\n";
    std::cout << "============================================================\n";

    demo_single_module();
    demo_linear_chain();
    demo_diamond_deps();
    demo_top_module_detection();
    demo_black_boxes();
    demo_testbench_detection();
    demo_dot_f_output();
    demo_json_output();
    demo_multi_module_file();
    demo_pkg_and_interface();

    std::cout << "\n============================================================\n";
    std::cout << "     Demo complete. All filelist features exercised.\n";
    std::cout << "============================================================\n\n";

    return 0;
}
