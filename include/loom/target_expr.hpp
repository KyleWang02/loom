#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>

namespace loom {

using TargetSet = std::unordered_set<std::string>;

class TargetExpr {
public:
    enum Kind { Wildcard, Identifier, All, Any, Not };

    // Static constructors
    static TargetExpr wildcard();
    static TargetExpr identifier(std::string name);
    static TargetExpr all(std::vector<TargetExpr> children);
    static TargetExpr any(std::vector<TargetExpr> children);
    static TargetExpr negation(TargetExpr child);

    // Parse from string: "all(simulation, not(verilator))"
    static Result<TargetExpr> parse(const std::string& input);

    // Evaluate against active target set
    bool evaluate(const TargetSet& active) const;

    // Canonical serialization
    std::string to_string() const;

    Kind kind() const;

private:
    Kind kind_;
    std::string name_;                   // for Identifier
    std::vector<TargetExpr> children_;   // for All, Any, Not
};

// Validate target name: [a-zA-Z][a-zA-Z0-9_-]*
bool is_valid_target_name(const std::string& name);

// Parse comma-separated CLI target string: "sim,synth,fpga"
Result<TargetSet> parse_target_set(const std::string& input);

struct SourceGroup {
    std::optional<TargetExpr> target;
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
    std::vector<std::string> files;
};

// Filter groups by active target set. Groups with no target always pass.
std::vector<SourceGroup> filter_source_groups(
    const std::vector<SourceGroup>& groups,
    const TargetSet& active);

} // namespace loom
