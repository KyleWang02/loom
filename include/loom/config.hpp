#pragma once

#include <loom/result.hpp>
#include <loom/manifest.hpp>
#include <string>
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace loom {

// Layered configuration: global > workspace > local
// Lower layers override higher layers (local wins over workspace wins over global)
struct Config {
    LintConfig lint;
    BuildConfig build;
    // Track which build fields were explicitly set (for merge)
    bool build_pre_lint_set = false;
    bool build_lint_fatal_set = false;
    std::unordered_map<std::string, TargetConfig> targets;

    // Load from a TOML config file (global or project-level)
    static Result<Config> load(const std::string& path);

    // Parse from TOML string
    static Result<Config> parse(const std::string& toml_str);

    // Merge another config on top (other's values override this)
    void merge(const Config& other);

    // Build effective config from layers: global -> workspace -> local
    static Config effective(const std::optional<Config>& global,
                            const std::optional<Config>& workspace,
                            const std::optional<Config>& local);
};

// Discover the global config file path: ~/.loom/config.toml
std::string global_config_path();

} // namespace loom
