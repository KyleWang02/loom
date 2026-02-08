#include <catch2/catch.hpp>
#include <loom/filelist.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <loom/build_cache.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>

using namespace loom;
namespace fs = std::filesystem;

// Helper: lex + parse inline Verilog
static ParseResult lex_and_parse(const std::string& src,
                                  bool is_sv = false,
                                  const std::string& fname = "<test>") {
    auto lr = lex(src, fname, is_sv);
    REQUIRE(lr.is_ok());
    auto pr = parse(lr.value(), fname, is_sv);
    REQUIRE(pr.is_ok());
    return std::move(pr.value());
}

// Helper: create a SourceGroup from inline files
// Writes files to temp dir and returns source group
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("loom_test_fl_" +
            std::to_string(getpid()) + "_" + std::to_string(rand()));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }

    std::string write_file(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
        ofs.close();
        return p.string();
    }
};

// ===== Section 1: Graph Construction =====

TEST_CASE("filelist: single file single module", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("top.v",
        "module top(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().files.size() == 1);
    CHECK(r.value().files[0].file_path == f);
    CHECK(r.value().top_modules.size() == 1);
    CHECK(r.value().top_modules[0] == "top");
}

TEST_CASE("filelist: linear dependency chain", "[filelist]") {
    TempDir tmp;
    auto f_a = tmp.write_file("a.v",
        "module a(input x); b b_inst(); endmodule\n");
    auto f_b = tmp.write_file("b.v",
        "module b(input y); c c_inst(); endmodule\n");
    auto f_c = tmp.write_file("c.v",
        "module c(input z); endmodule\n");

    SourceGroup g;
    g.files = {f_a, f_b, f_c};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& files = r.value().files;
    REQUIRE(files.size() == 3);

    // Providers-first: c before b before a
    CHECK(files[0].file_path == f_c);
    CHECK(files[1].file_path == f_b);
    CHECK(files[2].file_path == f_a);
}

TEST_CASE("filelist: diamond dependency", "[filelist]") {
    TempDir tmp;
    auto f_top = tmp.write_file("top.v",
        "module top(input x); left l(); right r(); endmodule\n");
    auto f_left = tmp.write_file("left.v",
        "module left(input x); bottom b(); endmodule\n");
    auto f_right = tmp.write_file("right.v",
        "module right(input x); bottom b(); endmodule\n");
    auto f_bottom = tmp.write_file("bottom.v",
        "module bottom(input x); endmodule\n");

    SourceGroup g;
    g.files = {f_top, f_left, f_right, f_bottom};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& files = r.value().files;
    REQUIRE(files.size() == 4);

    // bottom must come before left and right; left and right before top
    // Find positions
    std::unordered_map<std::string, size_t> pos;
    for (size_t i = 0; i < files.size(); ++i) {
        pos[files[i].file_path] = i;
    }
    CHECK(pos[f_bottom] < pos[f_left]);
    CHECK(pos[f_bottom] < pos[f_right]);
    CHECK(pos[f_left] < pos[f_top]);
    CHECK(pos[f_right] < pos[f_top]);
}

TEST_CASE("filelist: multi-module file", "[filelist]") {
    TempDir tmp;
    auto f_both = tmp.write_file("both.v",
        "module inner(input x); endmodule\n"
        "module outer(input x); inner i(); endmodule\n");

    SourceGroup g;
    g.files = {f_both};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    // Single file, no file-level edges (self-edge skipped)
    CHECK(r.value().files.size() == 1);
    CHECK(r.value().top_modules.size() == 1);
    CHECK(r.value().top_modules[0] == "outer");
}

TEST_CASE("filelist: package import dependency", "[filelist]") {
    TempDir tmp;
    auto f_pkg = tmp.write_file("my_pkg.sv",
        "package my_pkg; endpackage\n");
    auto f_mod = tmp.write_file("user.sv",
        "module user(input x);\n"
        "  import my_pkg::*;\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_pkg, f_mod};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& files = r.value().files;
    REQUIRE(files.size() == 2);

    // Package before module that imports it
    std::unordered_map<std::string, size_t> pos;
    for (size_t i = 0; i < files.size(); ++i) {
        pos[files[i].file_path] = i;
    }
    CHECK(pos[f_pkg] < pos[f_mod]);
}

TEST_CASE("filelist: cycle detection", "[filelist]") {
    TempDir tmp;
    auto f_a = tmp.write_file("a.v",
        "module a(input x); b b_inst(); endmodule\n");
    auto f_b = tmp.write_file("b.v",
        "module b(input x); a a_inst(); endmodule\n");

    SourceGroup g;
    g.files = {f_a, f_b};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_err());
    CHECK(r.error().code == LoomError::Cycle);
}

// ===== Section 2: Top-Level Detection =====

TEST_CASE("filelist: single top module", "[filelist]") {
    TempDir tmp;
    auto f_top = tmp.write_file("top.v",
        "module top(input clk); child c(); endmodule\n");
    auto f_child = tmp.write_file("child.v",
        "module child(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f_top, f_child};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().top_modules == std::vector<std::string>{"top"});
}

TEST_CASE("filelist: multiple top modules", "[filelist]") {
    TempDir tmp;
    auto f_a = tmp.write_file("a.v",
        "module alpha(input clk); shared s(); endmodule\n");
    auto f_b = tmp.write_file("b.v",
        "module beta(input clk); shared s(); endmodule\n");
    auto f_s = tmp.write_file("shared.v",
        "module shared(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f_a, f_b, f_s};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& tops = r.value().top_modules;
    CHECK(tops.size() == 2);
    // sorted alphabetically
    CHECK(tops[0] == "alpha");
    CHECK(tops[1] == "beta");
}

TEST_CASE("filelist: explicit top option", "[filelist]") {
    TempDir tmp;
    auto f_a = tmp.write_file("a.v", "module a(input x); endmodule\n");
    auto f_b = tmp.write_file("b.v", "module b(input x); endmodule\n");

    SourceGroup g;
    g.files = {f_a, f_b};

    FilelistOptions opts;
    opts.top_module = "b";

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    CHECK(r.value().top_modules == std::vector<std::string>{"b"});
}

TEST_CASE("filelist: packages excluded from top modules", "[filelist]") {
    TempDir tmp;
    auto f_pkg = tmp.write_file("pkg.sv",
        "package util_pkg; endpackage\n");
    auto f_mod = tmp.write_file("top.sv",
        "module top(input clk);\n"
        "  import util_pkg::*;\n"
        "endmodule\n");

    SourceGroup g;
    g.files = {f_pkg, f_mod};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    // Only 'top' is a top module; util_pkg is a package
    CHECK(r.value().top_modules == std::vector<std::string>{"top"});
}

// ===== Section 3: Testbench Heuristics =====

TEST_CASE("filelist: testbench by name", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("foo_tb.v",
        "module foo_tb(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;
    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    CHECK(r.value().testbench_modules == std::vector<std::string>{"foo_tb"});
}

TEST_CASE("filelist: testbench by no ports", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("runner.v",
        "module runner; endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;
    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    // Module with no ports is detected as testbench
    auto& tbs = r.value().testbench_modules;
    CHECK(std::find(tbs.begin(), tbs.end(), "runner") != tbs.end());
}

TEST_CASE("filelist: testbench by program kind", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("prog.sv",
        "program my_prog(input clk); endprogram\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;
    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    auto& tbs = r.value().testbench_modules;
    CHECK(std::find(tbs.begin(), tbs.end(), "my_prog") != tbs.end());
}

TEST_CASE("filelist: testbench by path", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("tb/driver.v",
        "module driver(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;
    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    auto& tbs = r.value().testbench_modules;
    CHECK(std::find(tbs.begin(), tbs.end(), "driver") != tbs.end());
}

// ===== Section 4: Black Box Detection =====

TEST_CASE("filelist: single black box", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("top.v",
        "module top(input clk); memory_ip mem(); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().black_boxes == std::vector<std::string>{"memory_ip"});
}

TEST_CASE("filelist: no black boxes", "[filelist]") {
    TempDir tmp;
    auto f_a = tmp.write_file("a.v",
        "module a(input clk); b b_inst(); endmodule\n");
    auto f_b = tmp.write_file("b.v",
        "module b(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f_a, f_b};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().black_boxes.empty());
}

TEST_CASE("filelist: multiple black boxes", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("top.v",
        "module top(input clk); ram r(); rom ro(); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& bb = r.value().black_boxes;
    CHECK(bb.size() == 2);
    // sorted
    CHECK(bb[0] == "ram");
    CHECK(bb[1] == "rom");
}

// ===== Section 5: Output Format =====

TEST_CASE("filelist: dot_f format", "[filelist]") {
    FilelistResult result;
    result.files.push_back({"leaf.v", {"WIDTH=8"}, {"/inc"}});
    result.files.push_back({"top.v", {"WIDTH=8"}, {"/inc"}});

    auto dot_f = result.to_dot_f();
    CHECK(dot_f.find("+incdir+/inc") != std::string::npos);
    CHECK(dot_f.find("+define+WIDTH=8") != std::string::npos);
    CHECK(dot_f.find("leaf.v") != std::string::npos);
    CHECK(dot_f.find("top.v") != std::string::npos);

    // incdir/define appear only once (deduped)
    size_t count = 0;
    size_t pos = 0;
    while ((pos = dot_f.find("+incdir+/inc", pos)) != std::string::npos) {
        ++count;
        pos += 12;
    }
    CHECK(count == 1);
}

TEST_CASE("filelist: json format", "[filelist]") {
    FilelistResult result;
    result.files.push_back({"leaf.v", {}, {}});
    result.files.push_back({"top.v", {}, {}});
    result.top_modules = {"top"};
    result.black_boxes = {"ext_ip"};

    auto json = result.to_json();
    CHECK(json.find("\"leaf.v\"") != std::string::npos);
    CHECK(json.find("\"top.v\"") != std::string::npos);
    CHECK(json.find("\"top\"") != std::string::npos);
    CHECK(json.find("\"ext_ip\"") != std::string::npos);
}

TEST_CASE("filelist: incdir deduplication", "[filelist]") {
    FilelistResult result;
    result.files.push_back({"a.v", {}, {"/inc", "/lib"}});
    result.files.push_back({"b.v", {}, {"/inc", "/src"}});

    auto dot_f = result.to_dot_f();

    // Each unique incdir appears exactly once
    size_t inc_count = 0;
    size_t pos = 0;
    while ((pos = dot_f.find("+incdir+", pos)) != std::string::npos) {
        ++inc_count;
        pos += 8;
    }
    CHECK(inc_count == 3);  // /inc, /lib, /src
}

// ===== Section 6: Target Filtering =====

TEST_CASE("filelist: exclude non-matching groups", "[filelist]") {
    TempDir tmp;
    auto f_rtl = tmp.write_file("rtl.v",
        "module rtl(input clk); endmodule\n");
    auto f_sim = tmp.write_file("sim_only.v",
        "module sim_only(input clk); endmodule\n");

    SourceGroup g_rtl;
    g_rtl.files = {f_rtl};
    // No target = always included

    SourceGroup g_sim;
    g_sim.target = TargetExpr::identifier("simulation");
    g_sim.files = {f_sim};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.active_targets = {"synthesis"};  // no "simulation"

    auto r = gen.generate_from_groups({g_rtl, g_sim}, opts);
    REQUIRE(r.is_ok());
    // Only rtl should be included
    REQUIRE(r.value().files.size() == 1);
    CHECK(r.value().files[0].file_path == f_rtl);
}

TEST_CASE("filelist: include matching groups", "[filelist]") {
    TempDir tmp;
    auto f_rtl = tmp.write_file("rtl.v",
        "module rtl(input clk); endmodule\n");
    auto f_sim = tmp.write_file("sim_only.v",
        "module sim_mod(input clk); endmodule\n");

    SourceGroup g_rtl;
    g_rtl.files = {f_rtl};

    SourceGroup g_sim;
    g_sim.target = TargetExpr::identifier("simulation");
    g_sim.files = {f_sim};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.active_targets = {"simulation"};

    auto r = gen.generate_from_groups({g_rtl, g_sim}, opts);
    REQUIRE(r.is_ok());
    // Both groups included
    CHECK(r.value().files.size() == 2);
}

// ===== Section 7: Cache Integration =====

TEST_CASE("filelist: cache miss then hit", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("mod.v",
        "module mod(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    auto db_path = (tmp.path / "cache.db").string();
    BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    FilelistGenerator gen(&cache);

    // First call: cache miss, parses file
    auto r1 = gen.generate_from_groups({g});
    REQUIRE(r1.is_ok());
    CHECK(r1.value().files.size() == 1);

    // Second call: should use cached parse result
    auto r2 = gen.generate_from_groups({g});
    REQUIRE(r2.is_ok());
    CHECK(r2.value().files.size() == 1);
    CHECK(r2.value().files[0].file_path == f);
}

TEST_CASE("filelist: cache miss on change", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("mod.v",
        "module mod(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    auto db_path = (tmp.path / "cache.db").string();
    BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    FilelistGenerator gen(&cache);

    auto r1 = gen.generate_from_groups({g});
    REQUIRE(r1.is_ok());
    CHECK(r1.value().black_boxes.empty());

    // Modify file: now instantiates a black box
    {
        std::ofstream ofs(f);
        ofs << "module mod(input clk); ext_ip e(); endmodule\n";
    }

    auto r2 = gen.generate_from_groups({g});
    REQUIRE(r2.is_ok());
    CHECK(r2.value().black_boxes == std::vector<std::string>{"ext_ip"});
}

TEST_CASE("filelist: no cache (nullptr)", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("mod.v",
        "module mod(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen(nullptr);
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().files.size() == 1);
}

// ===== Section 8: Edge Cases =====

TEST_CASE("filelist: empty sources", "[filelist]") {
    FilelistGenerator gen;
    auto r = gen.generate_from_groups({});
    REQUIRE(r.is_ok());
    CHECK(r.value().files.empty());
    CHECK(r.value().top_modules.empty());
    CHECK(r.value().black_boxes.empty());
}

TEST_CASE("filelist: file with no design units", "[filelist]") {
    TempDir tmp;
    auto f_defs = tmp.write_file("defs.v",
        "`define WIDTH 8\n`define DEPTH 16\n");
    auto f_mod = tmp.write_file("mod.v",
        "module mod(input clk); endmodule\n");

    SourceGroup g;
    g.files = {f_defs, f_mod};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    auto& files = r.value().files;
    REQUIRE(files.size() == 2);

    // Define-only file should come first
    CHECK(files[0].file_path == f_defs);
    CHECK(files[1].file_path == f_mod);
}

TEST_CASE("filelist: SV extension detected", "[filelist]") {
    TempDir tmp;
    auto f = tmp.write_file("pkg.sv",
        "package my_pkg;\n"
        "  typedef logic [7:0] byte_t;\n"
        "endpackage\n");

    SourceGroup g;
    g.files = {f};

    FilelistGenerator gen;
    auto r = gen.generate_from_groups({g});
    REQUIRE(r.is_ok());
    CHECK(r.value().files.size() == 1);
    // Package detected (only works with SV mode enabled by .sv extension)
    CHECK(r.value().top_modules.empty());  // packages excluded from top modules
}

// ===== Section: Testbench filtering =====

TEST_CASE("filelist: testbenches excluded by default", "[filelist]") {
    TempDir tmp;
    auto f_dut = tmp.write_file("dut.v",
        "module dut(input clk, output out); endmodule\n");
    auto f_tb = tmp.write_file("dut_tb.v",
        "module dut_tb; dut d(); endmodule\n");

    SourceGroup g;
    g.files = {f_dut, f_tb};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = false;

    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    // Testbench file excluded from output
    REQUIRE(r.value().files.size() == 1);
    CHECK(r.value().files[0].file_path == f_dut);
    // But still detected
    CHECK(!r.value().testbench_modules.empty());
}

TEST_CASE("filelist: testbenches included when requested", "[filelist]") {
    TempDir tmp;
    auto f_dut = tmp.write_file("dut.v",
        "module dut(input clk, output out); endmodule\n");
    auto f_tb = tmp.write_file("dut_tb.v",
        "module dut_tb; dut d(); endmodule\n");

    SourceGroup g;
    g.files = {f_dut, f_tb};

    FilelistGenerator gen;
    FilelistOptions opts;
    opts.include_testbenches = true;

    auto r = gen.generate_from_groups({g}, opts);
    REQUIRE(r.is_ok());
    CHECK(r.value().files.size() == 2);
}
