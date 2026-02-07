#include <loom/config.hpp>
#include <tomlplusplus/toml.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace loom {

Result<Config> Config::parse(const std::string& toml_str) {
    toml::table doc;
    try {
        doc = toml::parse(toml_str);
    } catch (const toml::parse_error& e) {
        return LoomError{LoomError::Parse,
            std::string("config TOML parse error: ") + e.what()};
    }

    Config cfg;

    // [lint] section
    if (auto lint = doc["lint"].as_table()) {
        for (const auto& [key, val] : *lint) {
            std::string k(key);
            if (k == "naming") {
                if (auto naming = val.as_table()) {
                    for (const auto& [nk, nv] : *naming) {
                        if (auto s = nv.value<std::string>()) {
                            cfg.lint.naming[std::string(nk)] = std::string(*s);
                        }
                    }
                }
            } else {
                if (auto s = val.value<std::string>()) {
                    cfg.lint.rules[k] = std::string(*s);
                }
            }
        }
    }

    // [build] section
    if (auto build = doc["build"].as_table()) {
        if (auto v = (*build)["pre-lint"].value<bool>()) {
            cfg.build.pre_lint = *v;
            cfg.build_pre_lint_set = true;
        }
        if (auto v = (*build)["lint-fatal"].value<bool>()) {
            cfg.build.lint_fatal = *v;
            cfg.build_lint_fatal_set = true;
        }
    }

    // [targets.<name>] sections
    if (auto targets = doc["targets"].as_table()) {
        for (const auto& [key, val] : *targets) {
            if (auto tbl = val.as_table()) {
                TargetConfig tc;
                tc.name = std::string(key);
                if (auto v = (*tbl)["tool"].value<std::string>())
                    tc.tool = std::string(*v);
                if (auto v = (*tbl)["action"].value<std::string>())
                    tc.action = std::string(*v);
                if (auto opts = (*tbl)["options"].as_table()) {
                    for (const auto& [ok, ov] : *opts) {
                        if (auto s = ov.value<std::string>()) {
                            tc.options[std::string(ok)] = std::string(*s);
                        } else if (auto b = ov.value<bool>()) {
                            tc.options[std::string(ok)] = *b ? "true" : "false";
                        }
                    }
                }
                cfg.targets[tc.name] = std::move(tc);
            }
        }
    }

    return Result<Config>::ok(std::move(cfg));
}

Result<Config> Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return LoomError{LoomError::IO,
            "cannot open config file: " + path};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return Config::parse(ss.str());
}

void Config::merge(const Config& other) {
    // Lint rules: other overrides this
    for (const auto& [k, v] : other.lint.rules) {
        lint.rules[k] = v;
    }
    for (const auto& [k, v] : other.lint.naming) {
        lint.naming[k] = v;
    }

    // Build: other overrides only explicitly-set fields
    if (other.build_pre_lint_set) {
        build.pre_lint = other.build.pre_lint;
        build_pre_lint_set = true;
    }
    if (other.build_lint_fatal_set) {
        build.lint_fatal = other.build.lint_fatal;
        build_lint_fatal_set = true;
    }

    // Targets: other overrides this per-target
    for (const auto& [k, v] : other.targets) {
        targets[k] = v;
    }
}

Config Config::effective(const std::optional<Config>& global,
                          const std::optional<Config>& workspace,
                          const std::optional<Config>& local) {
    Config result;
    if (global.has_value()) result.merge(global.value());
    if (workspace.has_value()) result.merge(workspace.value());
    if (local.has_value()) result.merge(local.value());
    return result;
}

std::string global_config_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return "";
    return std::string(home) + "/.loom/config.toml";
}

} // namespace loom
