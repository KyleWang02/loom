#include <catch2/catch.hpp>
#include <loom/tool_driver.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unistd.h>

using namespace loom;
namespace fs = std::filesystem;

// Helper: build a minimal FilelistResult for testing
static FilelistResult make_filelist(
    const std::vector<std::string>& files = {"/src/pkg.sv", "/src/top.v"},
    const std::vector<std::string>& tops = {"top"},
    const std::vector<std::string>& tbs = {},
    const std::vector<std::string>& bbs = {}) {

    FilelistResult fl;
    for (auto& f : files) {
        FilelistEntry e;
        e.file_path = f;
        fl.files.push_back(e);
    }
    fl.top_modules = tops;
    fl.testbench_modules = tbs;
    fl.black_boxes = bbs;
    return fl;
}

// Helper: FilelistResult with defines and include dirs
static FilelistResult make_filelist_with_extras() {
    FilelistResult fl;
    {
        FilelistEntry e;
        e.file_path = "/src/defs.vh";
        e.defines = {"SYNTHESIS", "WIDTH=8"};
        e.include_dirs = {"/src/inc"};
        fl.files.push_back(e);
    }
    {
        FilelistEntry e;
        e.file_path = "/src/top.sv";
        e.include_dirs = {"/src/inc"};
        fl.files.push_back(e);
    }
    fl.top_modules = {"top"};
    return fl;
}

// Temp directory helper
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("loom_test_td_" +
            std::to_string(getpid()) + "_" + std::to_string(rand()));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

// Helper: check that a vector of strings contains a specific string
static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Helper: check that args vector contains a string
static bool args_contain(const ToolCommand& cmd, const std::string& s) {
    return contains(cmd.args, s);
}

// ===== Section 1: ToolAction parsing =====

TEST_CASE("tool_action: parse valid actions", "[tool_driver]") {
    auto r1 = parse_tool_action("lint");
    REQUIRE(r1.is_ok());
    CHECK(r1.value() == ToolAction::Lint);

    auto r2 = parse_tool_action("simulate");
    REQUIRE(r2.is_ok());
    CHECK(r2.value() == ToolAction::Simulate);

    auto r3 = parse_tool_action("synthesize");
    REQUIRE(r3.is_ok());
    CHECK(r3.value() == ToolAction::Synthesize);

    auto r4 = parse_tool_action("build");
    REQUIRE(r4.is_ok());
    CHECK(r4.value() == ToolAction::Build);
}

TEST_CASE("tool_action: reject invalid", "[tool_driver]") {
    auto r = parse_tool_action("compile");
    REQUIRE(r.is_err());
    CHECK(r.error().code == LoomError::InvalidArg);
}

TEST_CASE("tool_action: round-trip name", "[tool_driver]") {
    CHECK(tool_action_name(ToolAction::Lint) == "lint");
    CHECK(tool_action_name(ToolAction::Simulate) == "simulate");
    CHECK(tool_action_name(ToolAction::Synthesize) == "synthesize");
    CHECK(tool_action_name(ToolAction::Build) == "build");
}

// ===== Section 2: ToolOptions parsing =====

TEST_CASE("tool_options: CSV splitting", "[tool_driver]") {
    std::unordered_map<std::string, std::string> map = {
        {"compile_args", "-Wall, -Wno-fatal, -sv"}
    };
    auto opts = ToolOptions::from_map(map);
    REQUIRE(opts.compile_args.size() == 3);
    CHECK(opts.compile_args[0] == "-Wall");
    CHECK(opts.compile_args[1] == "-Wno-fatal");
    CHECK(opts.compile_args[2] == "-sv");
}

TEST_CASE("tool_options: boolean waveform", "[tool_driver]") {
    std::unordered_map<std::string, std::string> m1 = {{"waveform", "true"}};
    CHECK(ToolOptions::from_map(m1).waveform == true);

    std::unordered_map<std::string, std::string> m2 = {{"waveform", "1"}};
    CHECK(ToolOptions::from_map(m2).waveform == true);

    std::unordered_map<std::string, std::string> m3 = {{"waveform", "false"}};
    CHECK(ToolOptions::from_map(m3).waveform == false);

    std::unordered_map<std::string, std::string> m4 = {};
    CHECK(ToolOptions::from_map(m4).waveform == false);
}

TEST_CASE("tool_options: defaults for empty map", "[tool_driver]") {
    auto opts = ToolOptions::from_map({});
    CHECK(opts.compile_args.empty());
    CHECK(opts.timescale.empty());
    CHECK(opts.waveform == false);
    CHECK(opts.device.empty());
    CHECK(opts.top_module.empty());
    CHECK(opts.extra.empty());
}

TEST_CASE("tool_options: extra keys", "[tool_driver]") {
    std::unordered_map<std::string, std::string> map = {
        {"device", "xc7a35t"},
        {"my_custom_key", "my_value"},
        {"another", "thing"}
    };
    auto opts = ToolOptions::from_map(map);
    CHECK(opts.device == "xc7a35t");
    REQUIRE(opts.extra.size() == 2);
    CHECK(opts.extra.at("my_custom_key") == "my_value");
    CHECK(opts.extra.at("another") == "thing");
}

// ===== Section 3: Driver factory =====

TEST_CASE("factory: create by name", "[tool_driver]") {
    auto d1 = create_driver("icarus");
    REQUIRE(d1 != nullptr);
    CHECK(d1->name() == "icarus");

    auto d2 = create_driver("verilator");
    REQUIRE(d2 != nullptr);
    CHECK(d2->name() == "verilator");

    auto d3 = create_driver("yosys");
    REQUIRE(d3 != nullptr);
    CHECK(d3->name() == "yosys");
}

TEST_CASE("factory: unknown returns nullptr", "[tool_driver]") {
    auto d = create_driver("nonexistent_tool");
    CHECK(d == nullptr);
}

TEST_CASE("factory: available_drivers list", "[tool_driver]") {
    auto drivers = available_drivers();
    CHECK(drivers.size() == 9);
    CHECK(contains(drivers, "icarus"));
    CHECK(contains(drivers, "verilator"));
    CHECK(contains(drivers, "vivado-sim"));
    CHECK(contains(drivers, "vivado-synth"));
    CHECK(contains(drivers, "quartus"));
    CHECK(contains(drivers, "modelsim"));
    CHECK(contains(drivers, "vcs"));
    CHECK(contains(drivers, "xcelium"));
    CHECK(contains(drivers, "yosys"));
}

TEST_CASE("factory: create from TargetConfig", "[tool_driver]") {
    TargetConfig tc;
    tc.name = "sim";
    tc.tool = "icarus";
    tc.action = "simulate";
    auto d = create_driver(tc);
    REQUIRE(d != nullptr);
    CHECK(d->name() == "icarus");
}

TEST_CASE("factory: custom from TargetConfig", "[tool_driver]") {
    TargetConfig tc;
    tc.name = "my_custom";
    tc.tool = "custom";
    tc.action = "build";
    tc.options["build_cmd"] = "make -j4";
    auto d = create_driver(tc);
    REQUIRE(d != nullptr);
    CHECK(d->name() == "custom");
}

// ===== Section 4: Driver identity =====

TEST_CASE("driver: name and display_name", "[tool_driver]") {
    auto d = create_driver("icarus");
    CHECK(d->name() == "icarus");
    CHECK(d->display_name() == "Icarus Verilog");
    CHECK(d->executable_name() == "iverilog");
}

TEST_CASE("driver: supported_actions", "[tool_driver]") {
    auto icarus = create_driver("icarus");
    CHECK(icarus->supports(ToolAction::Lint));
    CHECK(icarus->supports(ToolAction::Simulate));
    CHECK_FALSE(icarus->supports(ToolAction::Synthesize));
    CHECK_FALSE(icarus->supports(ToolAction::Build));

    auto yosys = create_driver("yosys");
    CHECK(yosys->supports(ToolAction::Synthesize));
    CHECK_FALSE(yosys->supports(ToolAction::Simulate));
}

TEST_CASE("driver: all drivers have correct action sets", "[tool_driver]") {
    auto verilator = create_driver("verilator");
    CHECK(verilator->supports(ToolAction::Lint));
    CHECK(verilator->supports(ToolAction::Simulate));

    auto vivado_sim = create_driver("vivado-sim");
    CHECK(vivado_sim->supports(ToolAction::Simulate));
    CHECK_FALSE(vivado_sim->supports(ToolAction::Synthesize));

    auto vivado_synth = create_driver("vivado-synth");
    CHECK(vivado_synth->supports(ToolAction::Synthesize));
    CHECK(vivado_synth->supports(ToolAction::Build));

    auto quartus = create_driver("quartus");
    CHECK(quartus->supports(ToolAction::Synthesize));
    CHECK(quartus->supports(ToolAction::Build));

    auto modelsim = create_driver("modelsim");
    CHECK(modelsim->supports(ToolAction::Simulate));

    auto vcs = create_driver("vcs");
    CHECK(vcs->supports(ToolAction::Simulate));

    auto xcelium = create_driver("xcelium");
    CHECK(xcelium->supports(ToolAction::Simulate));
}

// ===== Section 5: IcarusDriver commands =====

TEST_CASE("icarus: simulate generates 2 commands", "[tool_driver]") {
    auto d = create_driver("icarus");
    auto fl = make_filelist();
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 2);

    // Compile step
    auto& compile = cmds.value()[0];
    CHECK(compile.args[0] == "iverilog");
    CHECK(args_contain(compile, "-o"));
    CHECK(args_contain(compile, "-f"));
    CHECK(args_contain(compile, "-s"));
    CHECK(args_contain(compile, "top"));

    // Run step
    auto& run = cmds.value()[1];
    CHECK(run.args[0] == "vvp");
}

TEST_CASE("icarus: compile_args propagation", "[tool_driver]") {
    auto d = create_driver("icarus");
    auto fl = make_filelist();
    ToolOptions opts;
    opts.compile_args = {"-Wall", "-g2012"};
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    auto& compile = cmds.value()[0];
    CHECK(args_contain(compile, "-Wall"));
    CHECK(args_contain(compile, "-g2012"));
}

TEST_CASE("icarus: top module from options override", "[tool_driver]") {
    auto d = create_driver("icarus");
    auto fl = make_filelist({"/src/a.v"}, {"auto_top"});
    ToolOptions opts;
    opts.top_module = "my_top";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    CHECK(args_contain(cmds.value()[0], "my_top"));
    CHECK_FALSE(args_contain(cmds.value()[0], "auto_top"));
}

// ===== Section 6: VerilatorDriver commands =====

TEST_CASE("verilator: lint-only mode", "[tool_driver]") {
    auto d = create_driver("verilator");
    auto fl = make_filelist();
    ToolOptions opts;  // No simulate_args, no --binary in compile_args → lint
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);  // Lint = single command
    CHECK(args_contain(cmds.value()[0], "--lint-only"));
    CHECK(args_contain(cmds.value()[0], "--top-module"));
}

TEST_CASE("verilator: simulate mode with --binary", "[tool_driver]") {
    auto d = create_driver("verilator");
    auto fl = make_filelist();
    ToolOptions opts;
    opts.compile_args = {"--binary"};
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 2);  // compile + run
    CHECK(args_contain(cmds.value()[0], "--binary"));
    // Second command is the binary
    CHECK(cmds.value()[1].args[0].find("Vtop") != std::string::npos);
}

TEST_CASE("verilator: waveform --trace-fst", "[tool_driver]") {
    auto d = create_driver("verilator");
    auto fl = make_filelist();
    ToolOptions opts;
    opts.waveform = true;
    opts.waveform_format = "fst";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    CHECK(args_contain(cmds.value()[0], "--trace-fst"));
}

// ===== Section 7: VivadoSimDriver commands =====

TEST_CASE("vivado-sim: 3 step pipeline", "[tool_driver]") {
    auto d = create_driver("vivado-sim");
    auto fl = make_filelist();
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 3);
    CHECK(cmds.value()[0].args[0] == "xvlog");
    CHECK(cmds.value()[1].args[0] == "xelab");
    CHECK(cmds.value()[2].args[0] == "xsim");
}

TEST_CASE("vivado-sim: top module propagated", "[tool_driver]") {
    auto d = create_driver("vivado-sim");
    auto fl = make_filelist({"/src/a.sv"}, {"my_mod"});
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    // xelab should have top module
    CHECK(args_contain(cmds.value()[1], "my_mod"));
    // xsim should have snapshot name
    CHECK(args_contain(cmds.value()[2], "my_mod_sim"));
}

// ===== Section 8: VivadoSynthDriver commands =====

TEST_CASE("vivado-synth: TCL script content", "[tool_driver]") {
    auto d = create_driver("vivado-synth");
    auto fl = make_filelist({"/src/pkg.sv", "/src/top.v"}, {"top"});
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);
    CHECK(cmds.value()[0].args[0] == "vivado");
    CHECK(args_contain(cmds.value()[0], "-mode"));
    CHECK(args_contain(cmds.value()[0], "batch"));

    // TCL content stored in description
    auto& tcl = cmds.value()[0].description;
    CHECK(tcl.find("read_verilog -sv /src/pkg.sv") != std::string::npos);
    CHECK(tcl.find("read_verilog /src/top.v") != std::string::npos);
    CHECK(tcl.find("synth_design -top top") != std::string::npos);
}

TEST_CASE("vivado-synth: device in TCL script", "[tool_driver]") {
    auto d = create_driver("vivado-synth");
    auto fl = make_filelist();
    ToolOptions opts;
    opts.device = "xczu9eg-ffvb1156-2-e";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    auto& tcl = cmds.value()[0].description;
    CHECK(tcl.find("xczu9eg-ffvb1156-2-e") != std::string::npos);
}

// ===== Section 9: YosysDriver commands =====

TEST_CASE("yosys: .ys script with read_verilog", "[tool_driver]") {
    auto d = create_driver("yosys");
    auto fl = make_filelist({"/src/pkg.sv", "/src/top.v"}, {"top"});
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);
    CHECK(cmds.value()[0].args[0] == "yosys");
    CHECK(args_contain(cmds.value()[0], "-s"));

    // Script content in description
    auto& ys = cmds.value()[0].description;
    CHECK(ys.find("read_verilog -sv /src/pkg.sv") != std::string::npos);
    CHECK(ys.find("read_verilog /src/top.v") != std::string::npos);
    CHECK(ys.find("synth -top top") != std::string::npos);
}

TEST_CASE("yosys: synth_args propagated", "[tool_driver]") {
    auto d = create_driver("yosys");
    auto fl = make_filelist();
    ToolOptions opts;
    opts.synth_args = {"-flatten", "-abc9"};
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    auto& ys = cmds.value()[0].description;
    CHECK(ys.find("-flatten") != std::string::npos);
    CHECK(ys.find("-abc9") != std::string::npos);
}

// ===== Section 10: CustomDriver commands =====

TEST_CASE("custom: swap substitution", "[tool_driver]") {
    TargetConfig tc;
    tc.name = "my_sim";
    tc.tool = "custom";
    tc.action = "simulate";
    tc.options["build_cmd"] = "my_tool -f {{ filelist }} -top {{ top }}";
    tc.options["run_cmd"] = "./sim_{{ top }}";

    auto d = create_driver(tc);
    auto fl = make_filelist({"/src/top.v"}, {"top"});
    ToolOptions opts = ToolOptions::from_map(tc.options);

    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 2);

    // build_cmd: check filelist and top were substituted
    auto& build = cmds.value()[0];
    CHECK(args_contain(build, "my_tool"));
    CHECK(args_contain(build, "-f"));
    // filelist path should contain work_dir
    bool has_flist = false;
    for (auto& a : build.args) {
        if (a.find("filelist.f") != std::string::npos) {
            has_flist = true;
            break;
        }
    }
    CHECK(has_flist);

    // run_cmd
    auto& run = cmds.value()[1];
    CHECK(run.args[0] == "./sim_top");
}

TEST_CASE("custom: unknown variable error", "[tool_driver]") {
    TargetConfig tc;
    tc.name = "bad";
    tc.tool = "custom";
    tc.action = "build";
    tc.options["build_cmd"] = "tool {{ undefined_var }}";

    auto d = create_driver(tc);
    auto fl = make_filelist();
    ToolOptions opts = ToolOptions::from_map(tc.options);

    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    // swap_template strict mode should error on undefined vars
    CHECK(cmds.is_err());
}

TEST_CASE("custom: all standard variables available", "[tool_driver]") {
    TargetConfig tc;
    tc.name = "vars_test";
    tc.tool = "custom";
    tc.action = "build";
    tc.options["build_cmd"] =
        "echo {{ filelist }} {{ top }} {{ work_dir }} {{ sources }} {{ include_dirs }} {{ defines }}";

    auto d = create_driver(tc);
    auto fl = make_filelist_with_extras();
    ToolOptions opts = ToolOptions::from_map(tc.options);

    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);

    // All variables should have been substituted (no {{ }} remaining)
    for (auto& a : cmds.value()[0].args) {
        CHECK(a.find("{{") == std::string::npos);
    }
}

// ===== Section 11: write_filelist helper =====

TEST_CASE("write_filelist: writes .f file", "[tool_driver]") {
    TempDir tmp;
    auto fl = make_filelist({"/src/a.v", "/src/b.sv"}, {"top"});
    auto path = (tmp.path / "test.f").string();

    auto r = ToolDriver::write_filelist(fl, path);
    REQUIRE(r.is_ok());
    CHECK(r.value() == path);
    CHECK(fs::exists(path));

    // Read and verify content matches to_dot_f()
    std::ifstream ifs(path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    CHECK(content == fl.to_dot_f());
}

TEST_CASE("write_filelist: invalid path returns error", "[tool_driver]") {
    auto fl = make_filelist();
    auto r = ToolDriver::write_filelist(fl, "/nonexistent_dir_12345/test.f");
    CHECK(r.is_err());
    CHECK(r.error().code == LoomError::IO);
}

// ===== Section 12: write_tcl_script helper =====

TEST_CASE("write_tcl_script: writes TCL to disk", "[tool_driver]") {
    TempDir tmp;
    auto path = (tmp.path / "test.tcl").string();
    std::string content = "puts \"hello\"\nexit\n";

    auto r = ToolDriver::write_tcl_script(content, path);
    REQUIRE(r.is_ok());
    CHECK(r.value() == path);

    std::ifstream ifs(path);
    std::string read_content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
    CHECK(read_content == content);
}

// ===== Section 13: resolve_top_module =====

TEST_CASE("resolve_top_module: option override wins", "[tool_driver]") {
    auto d = create_driver("icarus");
    auto fl = make_filelist({"/src/a.v"}, {"auto_top"});
    ToolOptions opts;
    opts.top_module = "explicit_top";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    CHECK(args_contain(cmds.value()[0], "explicit_top"));
}

TEST_CASE("resolve_top_module: fallback to filelist detection", "[tool_driver]") {
    auto d = create_driver("icarus");
    auto fl = make_filelist({"/src/a.v"}, {"detected_mod"});
    ToolOptions opts;  // no top_module set
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    CHECK(args_contain(cmds.value()[0], "detected_mod"));
}

// ===== Section 14: Quartus/ModelSim/VCS/Xcelium =====

TEST_CASE("quartus: command generation", "[tool_driver]") {
    auto d = create_driver("quartus");
    auto fl = make_filelist({"/src/top.v"}, {"top"});
    ToolOptions opts;
    opts.device = "EP4CE6E22C8";
    opts.family = "Cyclone IV E";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 2);

    // Step 1: quartus_sh -t project.tcl
    CHECK(cmds.value()[0].args[0] == "quartus_sh");
    CHECK(args_contain(cmds.value()[0], "-t"));

    // TCL stored in description
    auto& tcl = cmds.value()[0].description;
    CHECK(tcl.find("EP4CE6E22C8") != std::string::npos);
    CHECK(tcl.find("Cyclone IV E") != std::string::npos);
    CHECK(tcl.find("VERILOG_FILE") != std::string::npos);

    // Step 2: quartus_sh --flow compile
    CHECK(cmds.value()[1].args[0] == "quartus_sh");
    CHECK(args_contain(cmds.value()[1], "--flow"));
    CHECK(args_contain(cmds.value()[1], "compile"));
}

TEST_CASE("modelsim: command generation", "[tool_driver]") {
    auto d = create_driver("modelsim");
    auto fl = make_filelist({"/src/pkg.sv", "/src/top.v"}, {"top"});
    ToolOptions opts;
    opts.timescale = "1ns/1ps";
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);
    CHECK(cmds.value()[0].args[0] == "vsim");
    CHECK(args_contain(cmds.value()[0], "-batch"));
    CHECK(args_contain(cmds.value()[0], "-do"));

    // .do script in description
    auto& doscript = cmds.value()[0].description;
    CHECK(doscript.find("vlib work") != std::string::npos);
    CHECK(doscript.find("vlog -sv /src/pkg.sv") != std::string::npos);
    CHECK(doscript.find("vlog /src/top.v") != std::string::npos);
    CHECK(doscript.find("vsim -c top") != std::string::npos);
    CHECK(doscript.find("-t 1ns/1ps") != std::string::npos);
    CHECK(doscript.find("run -all") != std::string::npos);
}

TEST_CASE("vcs: command generation", "[tool_driver]") {
    auto d = create_driver("vcs");
    auto fl = make_filelist({"/src/top.v"}, {"top"});
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 2);

    // Compile
    CHECK(cmds.value()[0].args[0] == "vcs");
    CHECK(args_contain(cmds.value()[0], "-f"));
    CHECK(args_contain(cmds.value()[0], "-top"));
    CHECK(args_contain(cmds.value()[0], "top"));

    // Run
    auto& run = cmds.value()[1];
    CHECK(run.args[0].find("simv") != std::string::npos);
}

TEST_CASE("xcelium: single command", "[tool_driver]") {
    auto d = create_driver("xcelium");
    auto fl = make_filelist({"/src/top.v"}, {"top"});
    ToolOptions opts;
    auto cmds = d->generate_commands(fl, opts, "/tmp/work");
    REQUIRE(cmds.is_ok());
    REQUIRE(cmds.value().size() == 1);
    CHECK(cmds.value()[0].args[0] == "xrun");
    CHECK(args_contain(cmds.value()[0], "-f"));
    CHECK(args_contain(cmds.value()[0], "-top"));
    CHECK(args_contain(cmds.value()[0], "top"));
}
