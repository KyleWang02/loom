#include <loom/tool_driver.hpp>
#include <loom/git.hpp>   // run_command
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

// ---- ToolAction helpers ----

Result<ToolAction> parse_tool_action(const std::string& s) {
    if (s == "lint")       return Result<ToolAction>::ok(ToolAction::Lint);
    if (s == "simulate")   return Result<ToolAction>::ok(ToolAction::Simulate);
    if (s == "synthesize") return Result<ToolAction>::ok(ToolAction::Synthesize);
    if (s == "build")      return Result<ToolAction>::ok(ToolAction::Build);
    return LoomError(LoomError::InvalidArg,
                     "unknown tool action: '" + s + "'",
                     "valid actions: lint, simulate, synthesize, build");
}

std::string tool_action_name(ToolAction a) {
    switch (a) {
        case ToolAction::Lint:       return "lint";
        case ToolAction::Simulate:   return "simulate";
        case ToolAction::Synthesize: return "synthesize";
        case ToolAction::Build:      return "build";
    }
    return "unknown";
}

// ---- ToolOptions ----

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        auto start = item.find_first_not_of(" \t");
        auto end = item.find_last_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(item.substr(start, end - start + 1));
    }
    return result;
}

ToolOptions ToolOptions::from_map(const std::unordered_map<std::string, std::string>& map) {
    ToolOptions opts;

    auto get = [&](const std::string& key) -> std::string {
        auto it = map.find(key);
        return it != map.end() ? it->second : "";
    };

    auto get_csv = [&](const std::string& key) -> std::vector<std::string> {
        auto val = get(key);
        return val.empty() ? std::vector<std::string>{} : split_csv(val);
    };

    opts.compile_args    = get_csv("compile_args");
    opts.elaborate_args  = get_csv("elaborate_args");
    opts.simulate_args   = get_csv("simulate_args");
    opts.synth_args      = get_csv("synth_args");
    opts.timescale       = get("timescale");
    opts.waveform_format = get("waveform_format");
    opts.device          = get("device");
    opts.family          = get("family");
    opts.top_module      = get("top_module");

    auto wf = get("waveform");
    opts.waveform = (wf == "true" || wf == "1" || wf == "yes");

    // All unrecognized keys go into extra
    static const std::unordered_set<std::string> known_keys = {
        "compile_args", "elaborate_args", "simulate_args", "synth_args",
        "timescale", "waveform", "waveform_format", "device", "family",
        "top_module"
    };
    for (auto& [k, v] : map) {
        if (known_keys.find(k) == known_keys.end()) {
            opts.extra[k] = v;
        }
    }

    return opts;
}

// ---- ToolDriver base ----

bool ToolDriver::supports(ToolAction action) const {
    auto actions = supported_actions();
    return actions.find(action) != actions.end();
}

Result<std::string> ToolDriver::find_executable() const {
    auto exe = executable_name();
    // Search PATH
    std::string path_env;
    if (auto* p = std::getenv("PATH"))
        path_env = p;
    std::istringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        auto full = fs::path(dir) / exe;
        if (fs::exists(full) && !fs::is_directory(full))
            return Result<std::string>::ok(full.string());
    }
    return LoomError(LoomError::NotFound,
                     "executable '" + exe + "' not found in PATH");
}

Result<std::string> ToolDriver::detect_version() const {
    auto exe_result = find_executable();
    if (exe_result.is_err()) return std::move(exe_result).error();

    auto cmd_result = run_command({exe_result.value(), "--version"});
    if (cmd_result.is_err()) return std::move(cmd_result).error();
    if (cmd_result.value().exit_code != 0)
        return LoomError(LoomError::IO,
                         "failed to get version for " + name());

    return Result<std::string>::ok(cmd_result.value().stdout_str);
}

std::string ToolDriver::resolve_top_module(const FilelistResult& filelist,
                                            const ToolOptions& options) const {
    if (!options.top_module.empty())
        return options.top_module;
    if (!filelist.top_modules.empty())
        return filelist.top_modules.front();
    return "top";
}

Result<std::string> ToolDriver::write_filelist(const FilelistResult& filelist,
                                                const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs)
        return LoomError(LoomError::IO, "cannot write filelist: " + path);
    ofs << filelist.to_dot_f();
    ofs.close();
    return Result<std::string>::ok(path);
}

Result<std::string> ToolDriver::write_tcl_script(const std::string& content,
                                                   const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs)
        return LoomError(LoomError::IO, "cannot write TCL script: " + path);
    ofs << content;
    ofs.close();
    return Result<std::string>::ok(path);
}

Result<ToolResult> ToolDriver::execute(
    const FilelistResult& filelist,
    const TargetConfig& target,
    const std::string& build_root) {

    auto opts = ToolOptions::from_map(target.options);
    auto action_result = parse_tool_action(target.action);
    LOOM_TRY(action_result);

    if (!supports(action_result.value()))
        return LoomError(LoomError::InvalidArg,
                         name() + " does not support action '" + target.action + "'");

    // Ensure work directory exists
    auto work_dir = (fs::path(build_root) / target.name).string();
    fs::create_directories(work_dir);

    auto cmds = generate_commands(filelist, opts, work_dir);
    LOOM_TRY(cmds);

    ToolResult result;
    result.work_dir = work_dir;

    for (auto& cmd : cmds.value()) {
        auto dir = cmd.working_dir.empty() ? work_dir : cmd.working_dir;
        auto cr = run_command(cmd.args, dir);
        if (cr.is_err()) return std::move(cr).error();

        result.exit_code = cr.value().exit_code;
        result.stdout_log += cr.value().stdout_str;
        result.stderr_log += cr.value().stderr_str;

        if (result.exit_code != 0)
            return Result<ToolResult>::ok(std::move(result));
    }

    return Result<ToolResult>::ok(std::move(result));
}

// ---- Helper: collect source file paths ----

static std::vector<std::string> source_paths(const FilelistResult& filelist) {
    std::vector<std::string> paths;
    paths.reserve(filelist.files.size());
    for (auto& e : filelist.files)
        paths.push_back(e.file_path);
    return paths;
}

static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string result;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) result += sep;
        result += v[i];
    }
    return result;
}

// Collect all unique include dirs from the filelist
static std::vector<std::string> collect_include_dirs(const FilelistResult& filelist) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> dirs;
    for (auto& e : filelist.files) {
        for (auto& d : e.include_dirs) {
            if (seen.insert(d).second)
                dirs.push_back(d);
        }
    }
    return dirs;
}

// Collect all unique defines from the filelist
static std::vector<std::string> collect_defines(const FilelistResult& filelist) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> defs;
    for (auto& e : filelist.files) {
        for (auto& d : e.defines) {
            if (seen.insert(d).second)
                defs.push_back(d);
        }
    }
    return defs;
}

// ===== IcarusDriver =====

std::string IcarusDriver::name() const { return "icarus"; }
std::string IcarusDriver::display_name() const { return "Icarus Verilog"; }
std::string IcarusDriver::executable_name() const { return "iverilog"; }

std::unordered_set<ToolAction> IcarusDriver::supported_actions() const {
    return {ToolAction::Lint, ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> IcarusDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();
    auto vvp_path = (fs::path(work_dir) / "sim.vvp").string();

    std::vector<ToolCommand> cmds;

    // Compile step
    std::vector<std::string> compile_args = {"iverilog", "-o", vvp_path,
                                              "-f", flist_path, "-s", top};
    if (!options.timescale.empty()) {
        compile_args.push_back("-Wtimescale");
    }
    for (auto& a : options.compile_args)
        compile_args.push_back(a);

    // Lint mode: use -t null to just check syntax
    if (options.compile_args.empty() &&
        std::find(compile_args.begin(), compile_args.end(), "-t") == compile_args.end()) {
        // Check if caller requested lint-only via action (handled at execute level)
    }

    cmds.push_back({compile_args, work_dir, "Compile with Icarus Verilog"});

    // Simulate step (only for simulate, not lint)
    // The execute() method handles action filtering, but generate_commands
    // always produces the full pipeline. Caller can use only the first cmd for lint.
    std::vector<std::string> sim_args = {"vvp", vvp_path};
    for (auto& a : options.simulate_args)
        sim_args.push_back(a);

    cmds.push_back({sim_args, work_dir, "Run VVP simulation"});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== VerilatorDriver =====

std::string VerilatorDriver::name() const { return "verilator"; }
std::string VerilatorDriver::display_name() const { return "Verilator"; }
std::string VerilatorDriver::executable_name() const { return "verilator"; }

std::unordered_set<ToolAction> VerilatorDriver::supported_actions() const {
    return {ToolAction::Lint, ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> VerilatorDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();

    std::vector<ToolCommand> cmds;

    // Determine mode from compile_args hint or default to lint
    bool is_lint = true;
    for (auto& a : options.compile_args) {
        if (a == "--binary" || a == "--cc" || a == "--sc") {
            is_lint = false;
            break;
        }
    }
    // If simulate_args provided, assume simulate mode
    if (!options.simulate_args.empty())
        is_lint = false;

    if (is_lint && options.synth_args.empty() && options.elaborate_args.empty()) {
        // Lint-only mode
        std::vector<std::string> args = {"verilator", "--lint-only",
                                          "-f", flist_path, "--top-module", top};
        for (auto& a : options.compile_args)
            args.push_back(a);
        if (options.waveform && options.waveform_format == "fst")
            args.push_back("--trace-fst");
        else if (options.waveform)
            args.push_back("--trace");

        cmds.push_back({args, work_dir, "Lint with Verilator"});
    } else {
        // Simulate mode: compile to binary then run
        std::vector<std::string> args = {"verilator", "--binary",
                                          "-f", flist_path, "--top-module", top,
                                          "-o", "V" + top};
        for (auto& a : options.compile_args)
            args.push_back(a);
        if (options.waveform && options.waveform_format == "fst")
            args.push_back("--trace-fst");
        else if (options.waveform)
            args.push_back("--trace");

        cmds.push_back({args, work_dir, "Compile with Verilator"});

        // Run the compiled binary
        auto bin = (fs::path(work_dir) / "obj_dir" / ("V" + top)).string();
        std::vector<std::string> run_args = {bin};
        for (auto& a : options.simulate_args)
            run_args.push_back(a);

        cmds.push_back({run_args, work_dir, "Run Verilator simulation"});
    }

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== VivadoSimDriver =====

std::string VivadoSimDriver::name() const { return "vivado-sim"; }
std::string VivadoSimDriver::display_name() const { return "Vivado Simulator (xsim)"; }
std::string VivadoSimDriver::executable_name() const { return "xvlog"; }

std::unordered_set<ToolAction> VivadoSimDriver::supported_actions() const {
    return {ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> VivadoSimDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();
    auto snapshot = top + "_sim";

    std::vector<ToolCommand> cmds;

    // Step 1: xvlog
    std::vector<std::string> xvlog_args = {"xvlog", "-f", flist_path, "--sv"};
    for (auto& a : options.compile_args)
        xvlog_args.push_back(a);
    cmds.push_back({xvlog_args, work_dir, "Compile with xvlog"});

    // Step 2: xelab
    std::vector<std::string> xelab_args = {"xelab", top, "-s", snapshot};
    if (!options.timescale.empty()) {
        xelab_args.push_back("--timescale");
        xelab_args.push_back(options.timescale);
    }
    for (auto& a : options.elaborate_args)
        xelab_args.push_back(a);
    cmds.push_back({xelab_args, work_dir, "Elaborate with xelab"});

    // Step 3: xsim
    std::vector<std::string> xsim_args = {"xsim", snapshot, "-runall"};
    if (options.waveform) {
        xsim_args.push_back("--wdb");
        xsim_args.push_back((fs::path(work_dir) / "waves.wdb").string());
    }
    for (auto& a : options.simulate_args)
        xsim_args.push_back(a);
    cmds.push_back({xsim_args, work_dir, "Simulate with xsim"});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== VivadoSynthDriver =====

std::string VivadoSynthDriver::name() const { return "vivado-synth"; }
std::string VivadoSynthDriver::display_name() const { return "Vivado Synthesis"; }
std::string VivadoSynthDriver::executable_name() const { return "vivado"; }

std::unordered_set<ToolAction> VivadoSynthDriver::supported_actions() const {
    return {ToolAction::Synthesize, ToolAction::Build};
}

Result<std::vector<ToolCommand>> VivadoSynthDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto tcl_path = (fs::path(work_dir) / "build.tcl").string();

    // Generate TCL script
    std::ostringstream tcl;
    tcl << "# Loom-generated Vivado synthesis script\n";
    tcl << "create_project -in_memory -part " << (options.device.empty() ? "xc7a35tcpg236-1" : options.device) << "\n";

    auto paths = source_paths(filelist);
    for (auto& p : paths) {
        bool is_sv = (p.size() >= 3 && p.substr(p.size() - 3) == ".sv");
        if (is_sv)
            tcl << "read_verilog -sv " << p << "\n";
        else
            tcl << "read_verilog " << p << "\n";
    }

    auto inc_dirs = collect_include_dirs(filelist);
    if (!inc_dirs.empty()) {
        tcl << "set_property verilog_include_dirs {" << join(inc_dirs, " ") << "} [current_fileset]\n";
    }

    auto defines = collect_defines(filelist);
    for (auto& d : defines) {
        tcl << "set_property verilog_define {" << d << "} [current_fileset]\n";
    }

    tcl << "synth_design -top " << top;
    for (auto& a : options.synth_args)
        tcl << " " << a;
    tcl << "\n";

    tcl << "write_checkpoint -force " << (fs::path(work_dir) / "post_synth.dcp").string() << "\n";
    tcl << "report_utilization -file " << (fs::path(work_dir) / "utilization.rpt").string() << "\n";
    tcl << "report_timing_summary -file " << (fs::path(work_dir) / "timing.rpt").string() << "\n";

    std::vector<ToolCommand> cmds;
    cmds.push_back({{"vivado", "-mode", "batch", "-source", tcl_path},
                     work_dir, "Synthesize with Vivado"});

    // Store TCL content in description for testing (will be written at execute time)
    cmds.front().description = tcl.str();

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== QuartusDriver =====

std::string QuartusDriver::name() const { return "quartus"; }
std::string QuartusDriver::display_name() const { return "Intel Quartus Prime"; }
std::string QuartusDriver::executable_name() const { return "quartus_sh"; }

std::unordered_set<ToolAction> QuartusDriver::supported_actions() const {
    return {ToolAction::Synthesize, ToolAction::Build};
}

Result<std::vector<ToolCommand>> QuartusDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto tcl_path = (fs::path(work_dir) / "project.tcl").string();

    // Generate TCL script
    std::ostringstream tcl;
    tcl << "# Loom-generated Quartus project script\n";
    tcl << "package require ::quartus::project\n";
    tcl << "project_new " << top << " -overwrite\n";

    if (!options.device.empty())
        tcl << "set_global_assignment -name DEVICE " << options.device << "\n";
    if (!options.family.empty())
        tcl << "set_global_assignment -name FAMILY \"" << options.family << "\"\n";

    tcl << "set_global_assignment -name TOP_LEVEL_ENTITY " << top << "\n";

    auto paths = source_paths(filelist);
    for (auto& p : paths) {
        bool is_sv = (p.size() >= 3 && p.substr(p.size() - 3) == ".sv");
        tcl << "set_global_assignment -name "
            << (is_sv ? "SYSTEMVERILOG_FILE" : "VERILOG_FILE")
            << " " << p << "\n";
    }

    for (auto& a : options.synth_args)
        tcl << a << "\n";

    tcl << "project_close\n";

    std::vector<ToolCommand> cmds;

    // Step 1: Create project
    cmds.push_back({{"quartus_sh", "-t", tcl_path},
                     work_dir, tcl.str()});

    // Step 2: Compile
    cmds.push_back({{"quartus_sh", "--flow", "compile", top},
                     work_dir, "Compile with Quartus"});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== ModelSimDriver =====

std::string ModelSimDriver::name() const { return "modelsim"; }
std::string ModelSimDriver::display_name() const { return "ModelSim / QuestaSim"; }
std::string ModelSimDriver::executable_name() const { return "vsim"; }

std::unordered_set<ToolAction> ModelSimDriver::supported_actions() const {
    return {ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> ModelSimDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto do_path = (fs::path(work_dir) / "run.do").string();

    // Generate .do script
    std::ostringstream doscript;
    doscript << "# Loom-generated ModelSim script\n";
    doscript << "vlib work\n";
    doscript << "vmap work work\n";

    auto paths = source_paths(filelist);
    for (auto& p : paths) {
        bool is_sv = (p.size() >= 3 && p.substr(p.size() - 3) == ".sv");
        doscript << "vlog " << (is_sv ? "-sv " : "") << p << "\n";
    }

    doscript << "vsim -c " << top;
    if (!options.timescale.empty())
        doscript << " -t " << options.timescale;
    for (auto& a : options.simulate_args)
        doscript << " " << a;
    doscript << "\n";

    if (options.waveform)
        doscript << "log -r /*\n";

    doscript << "run -all\n";
    doscript << "quit -f\n";

    std::vector<ToolCommand> cmds;
    cmds.push_back({{"vsim", "-batch", "-do", do_path},
                     work_dir, doscript.str()});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== VcsDriver =====

std::string VcsDriver::name() const { return "vcs"; }
std::string VcsDriver::display_name() const { return "Synopsys VCS"; }
std::string VcsDriver::executable_name() const { return "vcs"; }

std::unordered_set<ToolAction> VcsDriver::supported_actions() const {
    return {ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> VcsDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();
    auto simv_path = (fs::path(work_dir) / "simv").string();

    std::vector<ToolCommand> cmds;

    // Compile
    std::vector<std::string> compile_args = {"vcs", "-f", flist_path,
                                              "-o", simv_path, "-top", top};
    if (!options.timescale.empty()) {
        compile_args.push_back("-timescale=" + options.timescale);
    }
    for (auto& a : options.compile_args)
        compile_args.push_back(a);
    cmds.push_back({compile_args, work_dir, "Compile with VCS"});

    // Run
    std::vector<std::string> sim_args = {simv_path};
    for (auto& a : options.simulate_args)
        sim_args.push_back(a);
    cmds.push_back({sim_args, work_dir, "Run VCS simulation"});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== XceliumDriver =====

std::string XceliumDriver::name() const { return "xcelium"; }
std::string XceliumDriver::display_name() const { return "Cadence Xcelium"; }
std::string XceliumDriver::executable_name() const { return "xrun"; }

std::unordered_set<ToolAction> XceliumDriver::supported_actions() const {
    return {ToolAction::Simulate};
}

Result<std::vector<ToolCommand>> XceliumDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();

    std::vector<ToolCommand> cmds;

    std::vector<std::string> args = {"xrun", "-f", flist_path, "-top", top};
    if (!options.timescale.empty()) {
        args.push_back("-timescale");
        args.push_back(options.timescale);
    }
    for (auto& a : options.compile_args)
        args.push_back(a);
    for (auto& a : options.simulate_args)
        args.push_back(a);

    cmds.push_back({args, work_dir, "Run with Xcelium"});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== YosysDriver =====

std::string YosysDriver::name() const { return "yosys"; }
std::string YosysDriver::display_name() const { return "Yosys"; }
std::string YosysDriver::executable_name() const { return "yosys"; }

std::unordered_set<ToolAction> YosysDriver::supported_actions() const {
    return {ToolAction::Synthesize};
}

Result<std::vector<ToolCommand>> YosysDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto ys_path = (fs::path(work_dir) / "synth.ys").string();

    // Generate Yosys script
    std::ostringstream ys;
    ys << "# Loom-generated Yosys synthesis script\n";

    auto paths = source_paths(filelist);
    for (auto& p : paths) {
        bool is_sv = (p.size() >= 3 && p.substr(p.size() - 3) == ".sv");
        ys << "read_verilog " << (is_sv ? "-sv " : "") << p << "\n";
    }

    auto defines = collect_defines(filelist);
    for (auto& d : defines) {
        ys << "read_verilog -D" << d << "\n";
    }

    ys << "synth -top " << top;
    for (auto& a : options.synth_args)
        ys << " " << a;
    ys << "\n";

    ys << "write_json " << (fs::path(work_dir) / "synth.json").string() << "\n";

    std::vector<ToolCommand> cmds;
    cmds.push_back({{"yosys", "-s", ys_path},
                     work_dir, ys.str()});

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== CustomDriver =====

CustomDriver::CustomDriver(const TargetConfig& target) : target_(target) {}

std::string CustomDriver::name() const { return "custom"; }
std::string CustomDriver::display_name() const { return "Custom (" + target_.name + ")"; }
std::string CustomDriver::executable_name() const {
    auto it = target_.options.find("executable");
    return it != target_.options.end() ? it->second : "custom_tool";
}

std::unordered_set<ToolAction> CustomDriver::supported_actions() const {
    return {ToolAction::Lint, ToolAction::Simulate, ToolAction::Synthesize, ToolAction::Build};
}

Result<std::vector<ToolCommand>> CustomDriver::generate_commands(
    const FilelistResult& filelist, const ToolOptions& options,
    const std::string& work_dir) const {

    auto top = resolve_top_module(filelist, options);
    auto flist_path = (fs::path(work_dir) / "filelist.f").string();
    auto paths = source_paths(filelist);
    auto inc_dirs = collect_include_dirs(filelist);
    auto defines = collect_defines(filelist);

    // Build swap map
    SwapMap vars;
    vars["filelist"] = flist_path;
    vars["top"] = top;
    vars["work_dir"] = work_dir;
    vars["sources"] = join(paths, " ");
    vars["include_dirs"] = join(inc_dirs, " ");
    vars["defines"] = join(defines, " ");

    // Add all extra options as variables
    for (auto& [k, v] : options.extra)
        vars[k] = v;

    // Also add from target options directly
    for (auto& [k, v] : target_.options)
        vars[k] = v;

    std::vector<ToolCommand> cmds;

    // Process build_cmd
    auto build_it = target_.options.find("build_cmd");
    if (build_it != target_.options.end()) {
        auto resolved = swap_template(build_it->second, vars);
        LOOM_TRY(resolved);

        // Split on whitespace
        std::istringstream ss(resolved.value());
        std::vector<std::string> args;
        std::string tok;
        while (ss >> tok) args.push_back(tok);

        cmds.push_back({args, work_dir, "Custom build command"});
    }

    // Process run_cmd
    auto run_it = target_.options.find("run_cmd");
    if (run_it != target_.options.end()) {
        auto resolved = swap_template(run_it->second, vars);
        LOOM_TRY(resolved);

        std::istringstream ss(resolved.value());
        std::vector<std::string> args;
        std::string tok;
        while (ss >> tok) args.push_back(tok);

        cmds.push_back({args, work_dir, "Custom run command"});
    }

    if (cmds.empty()) {
        return LoomError(LoomError::InvalidArg,
                         "custom driver requires 'build_cmd' or 'run_cmd' in options");
    }

    return Result<std::vector<ToolCommand>>::ok(std::move(cmds));
}

// ===== Factory functions =====

std::unique_ptr<ToolDriver> create_driver(const std::string& tool_name) {
    if (tool_name == "icarus")       return std::make_unique<IcarusDriver>();
    if (tool_name == "verilator")    return std::make_unique<VerilatorDriver>();
    if (tool_name == "vivado-sim")   return std::make_unique<VivadoSimDriver>();
    if (tool_name == "vivado-synth") return std::make_unique<VivadoSynthDriver>();
    if (tool_name == "quartus")      return std::make_unique<QuartusDriver>();
    if (tool_name == "modelsim")     return std::make_unique<ModelSimDriver>();
    if (tool_name == "vcs")          return std::make_unique<VcsDriver>();
    if (tool_name == "xcelium")      return std::make_unique<XceliumDriver>();
    if (tool_name == "yosys")        return std::make_unique<YosysDriver>();
    return nullptr;
}

std::unique_ptr<ToolDriver> create_driver(const TargetConfig& target) {
    if (target.tool == "custom")
        return std::make_unique<CustomDriver>(target);
    return create_driver(target.tool);
}

std::vector<std::string> available_drivers() {
    return {"icarus", "verilator", "vivado-sim", "vivado-synth",
            "quartus", "modelsim", "vcs", "xcelium", "yosys"};
}

Result<std::unique_ptr<ToolDriver>> detect_driver(ToolAction action) {
    std::vector<std::string> priority;

    switch (action) {
        case ToolAction::Simulate:
            priority = {"verilator", "icarus", "vivado-sim", "modelsim", "vcs", "xcelium"};
            break;
        case ToolAction::Synthesize:
            priority = {"vivado-synth", "quartus", "yosys"};
            break;
        case ToolAction::Lint:
            priority = {"verilator", "icarus"};
            break;
        case ToolAction::Build:
            priority = {"vivado-synth", "quartus"};
            break;
    }

    for (auto& name : priority) {
        auto driver = create_driver(name);
        if (driver && driver->find_executable().is_ok())
            return Result<std::unique_ptr<ToolDriver>>::ok(std::move(driver));
    }

    return LoomError(LoomError::NotFound,
                     "no EDA tool found for action '" + tool_action_name(action) + "'",
                     "install one of: " + join(priority, ", "));
}

} // namespace loom
