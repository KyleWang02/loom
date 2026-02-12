#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/lint.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_lint(CliArgs& /*global*/, CliArgs& cmd) {
    auto& positional = cmd.positional();

    // Determine file list: either positional args or project sources
    std::vector<std::string> files;
    LintConfig lint_config;
    bool have_project = false;

    if (!positional.empty()) {
        // Lint specific files given on the command line
        for (auto& p : positional) {
            auto abs = fs::absolute(p).string();
            if (!fs::exists(abs)) {
                return LoomError(LoomError::NotFound,
                    "file not found: " + p);
            }
            files.push_back(std::move(abs));
        }

        // Try to discover project for lint config (best-effort)
        auto proj_r = Project::discover(fs::current_path());
        if (proj_r.is_ok()) {
            lint_config = proj_r.value().manifest.lint;
            have_project = true;
        }
    } else {
        // Discover project and collect all sources
        auto proj_r = Project::discover(fs::current_path());
        if (proj_r.is_err()) return std::move(proj_r).error();
        auto& project = proj_r.value();

        lint_config = project.manifest.lint;
        have_project = true;

        auto sources_r = project.collect_sources(TargetSet{});
        if (sources_r.is_err()) return std::move(sources_r).error();
        files = std::move(sources_r).value();

        if (files.empty()) {
            log::info("no source files found");
            return Result<int>::ok(0);
        }
    }

    // Parse flags
    std::string format = cmd.has("format") ? cmd.get("format") : "text";
    bool strict = cmd.has("strict");
    auto rule_filters = cmd.get_all("rule");

    // Apply --severity override to all rules in config
    if (cmd.has("severity")) {
        std::string sev_str = cmd.get("severity");
        // Validate the severity string
        auto sev = lint::severity_from_string(sev_str);
        if (sev_str != "off" && sev_str != "warn" && sev_str != "error") {
            return LoomError(LoomError::InvalidArg,
                "invalid severity '" + sev_str + "'",
                "use 'off', 'warn', or 'error'");
        }
        // Override all existing rule severities
        for (auto& [rule_id, _] : lint_config.rules) {
            _ = sev_str;
        }
        // If no rules configured yet, the engine uses defaults;
        // we store the override string so we can apply it after configure
        (void)sev;
    }

    // Validate format
    if (format != "text" && format != "json") {
        return LoomError(LoomError::InvalidArg,
            "invalid format '" + format + "'",
            "use 'text' or 'json'");
    }

    // Configure lint engine
    lint::LintEngine engine;
    if (have_project) {
        engine.configure(lint_config);
    }

    // If --severity was given, apply a blanket override after configure
    if (cmd.has("severity")) {
        std::string sev_str = cmd.get("severity");
        // Re-configure with all rules set to this severity
        // We create a config that overrides everything
        LintConfig override_config = lint_config;
        auto all_rules = lint::create_all_rules();
        for (auto& rule : all_rules) {
            override_config.rules[rule->id()] = sev_str;
        }
        engine.configure(override_config);
    }

    // Run lint
    log::info("linting %zu file(s)...", files.size());

    auto report_r = engine.lint_files(files);
    if (report_r.is_err()) return std::move(report_r).error();
    auto report = std::move(report_r).value();

    // Filter diagnostics by --rule if specified
    if (!rule_filters.empty()) {
        std::unordered_set<std::string> allowed(rule_filters.begin(),
                                                  rule_filters.end());
        std::vector<lint::LintDiagnostic> filtered;
        int warn_count = 0;
        int error_count = 0;

        for (auto& diag : report.diagnostics) {
            if (allowed.count(diag.rule_id)) {
                if (diag.severity == lint::Severity::Warn) ++warn_count;
                if (diag.severity == lint::Severity::Error) ++error_count;
                filtered.push_back(std::move(diag));
            }
        }
        report.diagnostics = std::move(filtered);
        report.warn_count = warn_count;
        report.error_count = error_count;
    }

    // Output
    if (format == "json") {
        std::cout << report.to_json() << "\n";
    } else {
        for (auto& diag : report.diagnostics) {
            std::cout << diag.format() << "\n";
        }

        // Summary
        if (report.diagnostics.empty()) {
            std::cout << "No issues found in " << report.files_checked
                      << " file(s).\n";
        } else {
            std::cout << "\n" << report.diagnostics.size() << " issue(s): "
                      << report.error_count << " error(s), "
                      << report.warn_count << " warning(s) in "
                      << report.files_checked << " file(s).\n";
        }
    }

    // Exit code
    if (report.error_count > 0) {
        return Result<int>::ok(1);
    }
    if (strict && report.warn_count > 0) {
        return Result<int>::ok(1);
    }

    return Result<int>::ok(0);
}

void register_lint(CliParser& cli) {
    Command cmd;
    cmd.name = "lint";
    cmd.summary = "Lint Verilog/SystemVerilog source files";
    cmd.description = "Runs the loom lint engine on source files. If specific files are "
                      "given as arguments, only those are checked. Otherwise, all project "
                      "sources are discovered and linted. Lint rules are configured via "
                      "the [lint] section in Loom.toml.";
    cmd.usage = "loom lint [flags] [files...]";
    cmd.group = "Quality";
    cmd.flags = {
        Flag{
            "rule", "",
            "Only show diagnostics for this rule ID (repeatable)",
            true, "RULE", "", true
        },
        Flag{
            "severity", "",
            "Override severity for all rules ('warn' or 'error')",
            true, "LEVEL", "", false
        },
        Flag{
            "format", "",
            "Output format: 'text' (default) or 'json'",
            true, "FORMAT", "text", false
        },
        Flag{
            "strict", "",
            "Exit with code 1 if any warnings are found",
            false, "", "", false
        },
    };
    cmd.handler = handle_lint;
    cli.add_command(std::move(cmd));
}

} // namespace loom
