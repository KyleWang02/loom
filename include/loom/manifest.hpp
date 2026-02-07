#pragma once

#include <loom/result.hpp>
#include <loom/version.hpp>
#include <loom/name.hpp>
#include <loom/source.hpp>
#include <loom/target_expr.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace loom {

// [package] section
struct PackageInfo {
    std::string name;
    std::string version;
    std::string top;              // top-level module name
    std::vector<std::string> authors;
};

// [targets.<name>] section
struct TargetConfig {
    std::string name;             // target name (key in TOML)
    std::string tool;             // e.g., "verilator", "vivado-synth"
    std::string action;           // "simulate", "synthesize", "build", "lint"
    std::unordered_map<std::string, std::string> options;  // flat key-value
};

// [lint] section
struct LintConfig {
    // rule_id -> severity ("off", "warn", "error")
    std::unordered_map<std::string, std::string> rules;
    // [lint.naming] patterns
    std::unordered_map<std::string, std::string> naming;
};

// [build] section
struct BuildConfig {
    bool pre_lint = false;
    bool lint_fatal = false;
};

// [workspace] section
struct WorkspaceConfig {
    std::vector<std::string> members;
    std::vector<std::string> exclude;
    std::vector<std::string> default_members;
    // [workspace.dependencies] — same format as [dependencies]
    std::vector<Dependency> dependencies;
};

// Complete manifest
struct Manifest {
    PackageInfo package;
    std::vector<Dependency> dependencies;
    std::vector<SourceGroup> sources;
    std::unordered_map<std::string, TargetConfig> targets;
    LintConfig lint;
    BuildConfig build;
    std::optional<WorkspaceConfig> workspace;

    // Parse from TOML string
    static Result<Manifest> parse(const std::string& toml_str);

    // Parse from file path
    static Result<Manifest> load(const std::string& path);

    // Check if this is a workspace root (has [workspace] section)
    bool is_workspace() const;
};

} // namespace loom
