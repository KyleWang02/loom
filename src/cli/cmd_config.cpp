#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/config.hpp>
#include <loom/project.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static void print_effective_config(const Config& cfg) {
    std::cout << "Effective Configuration\n";
    std::cout << "-----------------------\n";

    // Build settings
    std::cout << "  [build]\n";
    std::cout << "    pre_lint     = " << (cfg.build.pre_lint ? "true" : "false") << "\n";
    std::cout << "    lint_fatal   = " << (cfg.build.lint_fatal ? "true" : "false") << "\n";

    // Lint rules
    if (!cfg.lint.rules.empty()) {
        std::cout << "  [lint.rules]\n";
        for (auto& [rule_id, severity] : cfg.lint.rules) {
            std::cout << "    " << rule_id << " = \"" << severity << "\"\n";
        }
    }

    // Lint naming patterns
    if (!cfg.lint.naming.empty()) {
        std::cout << "  [lint.naming]\n";
        for (auto& [pattern_name, pattern] : cfg.lint.naming) {
            std::cout << "    " << pattern_name << " = \"" << pattern << "\"\n";
        }
    }

    // Targets
    if (!cfg.targets.empty()) {
        std::cout << "  [targets]\n";
        for (auto& [tname, tcfg] : cfg.targets) {
            std::cout << "    [targets." << tname << "]\n";
            if (!tcfg.tool.empty()) {
                std::cout << "      tool   = \"" << tcfg.tool << "\"\n";
            }
            if (!tcfg.action.empty()) {
                std::cout << "      action = \"" << tcfg.action << "\"\n";
            }
            for (auto& [key, val] : tcfg.options) {
                std::cout << "      " << key << " = \"" << val << "\"\n";
            }
        }
    }
}

static Result<std::string> get_config_value(const Config& cfg, const std::string& key) {
    // Support dotted keys: build.pre_lint, build.lint_fatal, lint.rules.<id>, etc.
    if (key == "build.pre_lint") {
        return Result<std::string>::ok(cfg.build.pre_lint ? "true" : "false");
    }
    if (key == "build.lint_fatal") {
        return Result<std::string>::ok(cfg.build.lint_fatal ? "true" : "false");
    }

    // lint.rules.<rule_id>
    const std::string lint_rules_prefix = "lint.rules.";
    if (key.size() > lint_rules_prefix.size() &&
        key.substr(0, lint_rules_prefix.size()) == lint_rules_prefix) {
        std::string rule_id = key.substr(lint_rules_prefix.size());
        auto it = cfg.lint.rules.find(rule_id);
        if (it != cfg.lint.rules.end()) {
            return Result<std::string>::ok(it->second);
        }
        return LoomError(LoomError::NotFound,
            "lint rule '" + rule_id + "' not found in config");
    }

    // lint.naming.<name>
    const std::string lint_naming_prefix = "lint.naming.";
    if (key.size() > lint_naming_prefix.size() &&
        key.substr(0, lint_naming_prefix.size()) == lint_naming_prefix) {
        std::string pattern_name = key.substr(lint_naming_prefix.size());
        auto it = cfg.lint.naming.find(pattern_name);
        if (it != cfg.lint.naming.end()) {
            return Result<std::string>::ok(it->second);
        }
        return LoomError(LoomError::NotFound,
            "lint naming pattern '" + pattern_name + "' not found in config");
    }

    return LoomError(LoomError::NotFound,
        "unknown config key '" + key + "'",
        "valid keys: build.pre_lint, build.lint_fatal, lint.rules.<id>, lint.naming.<name>");
}

static Result<int> handle_config(CliArgs& /*global*/, CliArgs& cmd) {
    auto& pos = cmd.positional();

    // Load effective config
    std::optional<Config> global_cfg;
    std::string global_path = global_config_path();
    if (fs::exists(global_path)) {
        auto cfg_r = Config::load(global_path);
        if (cfg_r.is_ok()) {
            global_cfg = std::move(cfg_r).value();
        }
    }

    // Try to load project-local config
    std::optional<Config> local_cfg;
    fs::path cwd = fs::current_path();
    auto proj_r = Project::discover(cwd);
    std::filesystem::path project_root;
    if (proj_r.is_ok()) {
        project_root = proj_r.value().root_dir;
        // Project manifest may contain [lint] and [build] sections
        auto& manifest = proj_r.value().manifest;
        Config proj_config;
        proj_config.lint = manifest.lint;
        proj_config.build = manifest.build;
        proj_config.targets = manifest.targets;
        local_cfg = std::move(proj_config);
    }

    Config effective = Config::effective(global_cfg, std::nullopt, local_cfg);

    if (pos.empty()) {
        // No args: print all effective config
        print_effective_config(effective);
        return Result<int>::ok(0);
    }

    if (pos.size() == 1) {
        // One arg: print specific key
        auto val_r = get_config_value(effective, pos[0]);
        if (val_r.is_err()) {
            return LoomError(val_r.error().code, val_r.error().message, val_r.error().hint);
        }
        std::cout << pos[0] << " = " << val_r.value() << "\n";
        return Result<int>::ok(0);
    }

    // Two args: set key = value in local config file
    const std::string& key = pos[0];
    const std::string& value = pos[1];

    // Determine config file path (project-local or global)
    std::string config_path;
    if (!project_root.empty()) {
        config_path = (project_root / ".loom" / "config.toml").string();
    } else {
        config_path = global_path;
    }

    // Ensure parent directory exists
    fs::path config_dir = fs::path(config_path).parent_path();
    std::error_code ec;
    fs::create_directories(config_dir, ec);
    if (ec) {
        return LoomError(LoomError::IO,
            "failed to create config directory: " + ec.message());
    }

    // Load existing config file or start fresh
    std::string existing_content;
    if (fs::exists(config_path)) {
        std::ifstream ifs(config_path);
        if (ifs) {
            existing_content.assign(
                std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>());
        }
    }

    // Append the key-value setting as TOML
    // For simplicity, we append a section line if needed
    std::string toml_line;
    if (key == "build.pre_lint" || key == "build.lint_fatal") {
        std::string field = key.substr(6); // after "build."
        toml_line = "\n[build]\n" + field + " = " + value + "\n";
    } else if (key.substr(0, 11) == "lint.rules.") {
        std::string rule_id = key.substr(11);
        toml_line = "\n[lint.rules]\n" + rule_id + " = \"" + value + "\"\n";
    } else if (key.substr(0, 12) == "lint.naming.") {
        std::string pattern_name = key.substr(12);
        toml_line = "\n[lint.naming]\n" + pattern_name + " = \"" + value + "\"\n";
    } else {
        return LoomError(LoomError::InvalidArg,
            "cannot set unknown config key '" + key + "'",
            "valid keys: build.pre_lint, build.lint_fatal, lint.rules.<id>, lint.naming.<name>");
    }

    // Write to config file
    std::ofstream ofs(config_path, std::ios::app);
    if (!ofs) {
        return LoomError(LoomError::IO,
            "failed to write config file: " + config_path);
    }
    ofs << toml_line;

    log::info("set %s = %s in %s", key.c_str(), value.c_str(), config_path.c_str());
    std::cout << "Set " << key << " = " << value << " in " << config_path << "\n";

    return Result<int>::ok(0);
}

void register_config(CliParser& cli) {
    Command cmd;
    cmd.name = "config";
    cmd.summary = "View or modify loom configuration";
    cmd.description = "Without arguments, prints the effective configuration. "
                      "With one argument, prints the value of a specific key. "
                      "With two arguments, sets a key-value pair in the local config file.";
    cmd.usage = "loom config [key] [value]";
    cmd.group = "Project";
    cmd.flags = {};
    cmd.handler = handle_config;
    cli.add_command(std::move(cmd));
}

} // namespace loom
