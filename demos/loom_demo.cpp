// loom-demo — Run loom's lint, filelist, and resolve on real files.
//
// Usage:
//   loom-demo lint   <file.v|file.sv> [file2 ...] [--json] [--rule <id>] [--config <Loom.toml>]
//   loom-demo filelist <file1> [file2 ...] [--json] [--top <module>]
//   loom-demo resolve <project-dir>

#include <loom/lint.hpp>
#include <loom/filelist.hpp>
#include <loom/resolver.hpp>
#include <loom/lockfile.hpp>
#include <loom/cache.hpp>
#include <loom/manifest.hpp>
#include <loom/log.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace loom;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool has_flag(int argc, char* argv[], const char* flag) {
    for (int i = 0; i < argc; ++i)
        if (std::string(argv[i]) == flag) return true;
    return false;
}

static std::string get_option(int argc, char* argv[], const char* key) {
    for (int i = 0; i < argc - 1; ++i)
        if (std::string(argv[i]) == key) return argv[i + 1];
    return {};
}

static std::vector<std::string> collect_files(int argc, char* argv[], int start) {
    std::vector<std::string> files;
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-') {
            // skip flags and their values
            if (arg == "--rule" || arg == "--config" || arg == "--top") ++i;
            continue;
        }
        auto p = fs::absolute(arg);
        if (fs::is_regular_file(p)) {
            files.push_back(p.string());
        } else if (fs::is_directory(p)) {
            // recursively collect .v and .sv files
            for (auto& entry : fs::recursive_directory_iterator(p)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                if (ext == ".v" || ext == ".sv" || ext == ".vl" || ext == ".vlg")
                    files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

// ---------------------------------------------------------------------------
// lint subcommand
// ---------------------------------------------------------------------------

static int cmd_lint(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: loom-demo lint <file|dir> [file2 ...] [--json] [--rule <id>] [--config <Loom.toml>]\n";
        return 1;
    }

    bool json_out = has_flag(argc, argv, "--json");
    std::string filter_rule = get_option(argc, argv, "--rule");
    std::string config_path = get_option(argc, argv, "--config");

    auto files = collect_files(argc, argv, 2);
    if (files.empty()) {
        std::cerr << "error: no .v/.sv files found\n";
        return 1;
    }

    // Load config from Loom.toml if provided
    LintConfig lint_config;
    if (!config_path.empty()) {
        auto manifest = Manifest::load(config_path);
        if (manifest.is_ok()) {
            lint_config = manifest.value().lint;
        } else {
            std::cerr << "warning: could not load config: " << manifest.error().message << "\n";
        }
    }

    lint::LintEngine engine;
    engine.configure(lint_config);

    if (files.size() == 1) {
        auto result = engine.lint_file(files[0]);
        if (result.is_err()) {
            std::cerr << "error: " << result.error().message << "\n";
            return 1;
        }
        auto& report = result.value();

        if (json_out) {
            std::cout << report.to_json() << "\n";
        } else {
            for (auto& d : report.diagnostics) {
                if (!filter_rule.empty() && d.rule_id != filter_rule) continue;
                std::cout << d.format() << "\n";
            }
            std::cerr << "\n" << report.files_checked << " file(s), "
                      << report.warn_count << " warning(s), "
                      << report.error_count << " error(s)\n";
        }
        return report.error_count > 0 ? 1 : 0;

    } else {
        auto result = engine.lint_files(files);
        if (result.is_err()) {
            std::cerr << "error: " << result.error().message << "\n";
            return 1;
        }
        auto& report = result.value();

        if (json_out) {
            std::cout << report.to_json() << "\n";
        } else {
            for (auto& d : report.diagnostics) {
                if (!filter_rule.empty() && d.rule_id != filter_rule) continue;
                std::cout << d.format() << "\n";
            }
            std::cerr << "\n" << report.files_checked << " file(s), "
                      << report.warn_count << " warning(s), "
                      << report.error_count << " error(s)\n";
        }
        return report.error_count > 0 ? 1 : 0;
    }
}

// ---------------------------------------------------------------------------
// filelist subcommand
// ---------------------------------------------------------------------------

static int cmd_filelist(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: loom-demo filelist <file|dir> [file2 ...] [--json] [--top <module>]\n";
        return 1;
    }

    bool json_out = has_flag(argc, argv, "--json");
    std::string top_override = get_option(argc, argv, "--top");

    auto files = collect_files(argc, argv, 2);
    if (files.empty()) {
        std::cerr << "error: no .v/.sv files found\n";
        return 1;
    }

    SourceGroup group;
    group.files = files;

    FilelistGenerator gen;
    FilelistOptions opts;
    if (!top_override.empty()) opts.top_module = top_override;
    opts.include_testbenches = true;

    auto result = gen.generate_from_groups({group}, opts);
    if (result.is_err()) {
        std::cerr << "error: " << result.error().message << "\n";
        return 1;
    }

    auto& fl = result.value();

    if (json_out) {
        std::cout << fl.to_json() << "\n";
    } else {
        // Print .f content to stdout
        std::cout << fl.to_dot_f();

        // Print analysis to stderr
        std::cerr << "\n--- analysis ---\n";
        std::cerr << "Files: " << fl.files.size() << " (topologically sorted, providers first)\n";

        if (!fl.top_modules.empty()) {
            std::cerr << "Top modules:";
            for (auto& t : fl.top_modules) std::cerr << " " << t;
            std::cerr << "\n";
        }
        if (!fl.testbench_modules.empty()) {
            std::cerr << "Testbenches:";
            for (auto& t : fl.testbench_modules) std::cerr << " " << t;
            std::cerr << "\n";
        }
        if (!fl.black_boxes.empty()) {
            std::cerr << "Black boxes:";
            for (auto& b : fl.black_boxes) std::cerr << " " << b;
            std::cerr << "\n";
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// resolve subcommand
// ---------------------------------------------------------------------------

static int cmd_resolve(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: loom-demo resolve <project-dir> [--save]\n";
        return 1;
    }

    std::string project_dir = fs::absolute(argv[2]).string();
    bool save = has_flag(argc, argv, "--save");

    auto manifest_path = fs::path(project_dir) / "Loom.toml";
    if (!fs::exists(manifest_path)) {
        std::cerr << "error: no Loom.toml in " << project_dir << "\n";
        return 1;
    }

    auto manifest = Manifest::load(manifest_path.string());
    if (manifest.is_err()) {
        std::cerr << "error: " << manifest.error().message << "\n";
        return 1;
    }

    auto& m = manifest.value();
    std::cerr << "Package: " << m.package.name << " v" << m.package.version << "\n";
    std::cerr << "Dependencies: " << m.dependencies.size() << "\n";

    if (m.dependencies.empty()) {
        std::cerr << "No dependencies to resolve.\n";
        return 0;
    }

    // Check for existing lockfile
    auto lock_path = fs::path(project_dir) / "Loom.lock";
    std::optional<LockFile> existing;
    if (fs::exists(lock_path)) {
        auto lf = LockFile::load(lock_path.string());
        if (lf.is_ok()) {
            existing = std::move(lf.value());
            std::cerr << "Existing Loom.lock: " << existing->packages.size() << " packages";
            if (existing->is_stale(m.dependencies))
                std::cerr << " (STALE)";
            else
                std::cerr << " (up to date)";
            std::cerr << "\n";
        }
    }

    // Resolve
    auto cache_root = (fs::path(project_dir) / ".loom" / "cache").string();
    CacheManager cache(cache_root);
    DependencyResolver resolver(cache);

    std::cerr << "\nResolving...\n";
    auto result = resolver.resolve(m, existing);
    if (result.is_err()) {
        std::cerr << "error: " << result.error().format() << "\n";
        return 1;
    }

    auto& lf = result.value();

    // Print resolved packages
    std::cout << "root: " << lf.root_name << " v" << lf.root_version << "\n\n";
    for (auto& pkg : lf.packages) {
        std::cout << pkg.name << " v" << pkg.version << "\n";
        std::cout << "  source:   " << pkg.source << "\n";
        std::cout << "  ref:      " << pkg.ref << "\n";
        std::cout << "  commit:   " << pkg.commit << "\n";
        std::cout << "  checksum: " << pkg.checksum << "\n";
        if (!pkg.dependencies.empty()) {
            std::cout << "  deps:    ";
            for (size_t i = 0; i < pkg.dependencies.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << pkg.dependencies[i];
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // Topological order
    auto topo = DependencyResolver::topological_sort(lf);
    if (topo.is_ok()) {
        std::cerr << "Build order:";
        for (auto& name : topo.value()) std::cerr << " " << name;
        std::cerr << "\n";
    }

    // Save if requested
    if (save) {
        auto status = lf.save(lock_path.string());
        if (status.is_ok())
            std::cerr << "Wrote " << lock_path.string() << "\n";
        else
            std::cerr << "error saving lockfile: " << status.error().message << "\n";
    }

    std::cerr << "\nResolved " << lf.packages.size() << " package(s)\n";
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void print_usage() {
    std::cerr << "loom-demo — exercise loom's lint, filelist, and resolve on real files\n\n"
              << "Usage:\n"
              << "  loom-demo lint     <file|dir> [...] [--json] [--rule <id>] [--config <Loom.toml>]\n"
              << "  loom-demo filelist <file|dir> [...] [--json] [--top <module>]\n"
              << "  loom-demo resolve  <project-dir>    [--save]\n\n"
              << "Examples:\n"
              << "  loom-demo lint rtl/                        # lint all .v/.sv in rtl/\n"
              << "  loom-demo lint top.sv sub.sv --json        # lint specific files, JSON output\n"
              << "  loom-demo lint rtl/ --rule blocking-in-ff  # filter to one rule\n"
              << "  loom-demo lint rtl/ --config Loom.toml     # use [lint] config from manifest\n"
              << "  loom-demo filelist rtl/                    # generate .f file from rtl/\n"
              << "  loom-demo filelist rtl/ --json             # JSON output\n"
              << "  loom-demo filelist rtl/ --top soc_top      # override top module\n"
              << "  loom-demo resolve .                        # resolve deps from ./Loom.toml\n"
              << "  loom-demo resolve . --save                 # resolve and write Loom.lock\n";
}

int main(int argc, char* argv[]) {
    log::set_level(log::Warn);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "lint")     return cmd_lint(argc, argv);
    if (cmd == "filelist") return cmd_filelist(argc, argv);
    if (cmd == "resolve")  return cmd_resolve(argc, argv);

    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    std::cerr << "unknown command: " << cmd << "\n\n";
    print_usage();
    return 1;
}
