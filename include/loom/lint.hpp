#pragma once

#include <loom/lang/ir.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/manifest.hpp>
#include <loom/result.hpp>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace loom::lint {

// ---------------------------------------------------------------------------
// Severity and categories
// ---------------------------------------------------------------------------

enum class Severity { Off, Warn, Error };

enum class RuleCategory { Correctness, Structure, Style };

// ---------------------------------------------------------------------------
// LintDiagnostic — one reported issue
// ---------------------------------------------------------------------------

struct LintDiagnostic {
    Severity severity = Severity::Warn;
    std::string rule_id;
    std::string message;
    std::string file;
    int line = 0;
    int col = 0;

    // GCC-compatible format: file:line:col: severity: [rule-id] message
    std::string format() const;

    // JSON object string
    std::string to_json() const;
};

// ---------------------------------------------------------------------------
// SuppressionMap — built from lexer comments
// ---------------------------------------------------------------------------

class SuppressionMap {
public:
    // Build from comment list (filters for Suppression kind)
    static SuppressionMap build(const std::vector<Comment>& comments);

    // Check if a rule is suppressed at a given line (same-line or next-line)
    bool is_suppressed(const std::string& rule_id, int line) const;

private:
    // Maps line -> set of suppressed rule IDs (or "*" for all)
    std::unordered_map<int, std::vector<std::string>> suppressions_;
};

// ---------------------------------------------------------------------------
// LintReport — aggregated results
// ---------------------------------------------------------------------------

struct LintReport {
    std::vector<LintDiagnostic> diagnostics;
    int warn_count = 0;
    int error_count = 0;
    int files_checked = 0;

    // Full JSON output
    std::string to_json() const;
};

// ---------------------------------------------------------------------------
// LintRule — abstract base for all rules
// ---------------------------------------------------------------------------

class LintRule {
public:
    virtual ~LintRule() = default;

    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual RuleCategory category() const = 0;
    virtual Severity default_severity() const = 0;

    // Per-unit check (most rules)
    virtual void check(const DesignUnit& unit,
                       const std::string& filename,
                       std::vector<LintDiagnostic>& out);

    // Per-file check (one-module-per-file, filename-match)
    virtual void check_file(const ParseResult& parse_result,
                            const std::string& filename,
                            std::vector<LintDiagnostic>& out);

    // Token-level check (ifdef-balance, implicit-net)
    virtual void check_tokens(const LexResult& lex_result,
                              const std::string& filename,
                              std::vector<LintDiagnostic>& out);

    // Cross-file project-level check (duplicate-module)
    virtual void check_project(
        const std::vector<std::pair<std::string, ParseResult>>& all_files,
        std::vector<LintDiagnostic>& out);

protected:
    // Helper to emit a diagnostic
    void emit(std::vector<LintDiagnostic>& out,
              Severity sev,
              const std::string& file,
              int line, int col,
              const std::string& msg) const;
};

// ---------------------------------------------------------------------------
// LintEngine — orchestrator
// ---------------------------------------------------------------------------

class LintEngine {
public:
    LintEngine();

    // Apply configuration overrides (rule severities + naming patterns)
    void configure(const LintConfig& config);

    // Lint a single file from path (lex + parse + check)
    Result<LintReport> lint_file(const std::string& path);

    // Lint pre-parsed data (reuse in build pipeline)
    LintReport lint_parsed(const LexResult& lex_result,
                           const ParseResult& parse_result,
                           const std::string& filename);

    // Multi-file lint with project-level rules
    Result<LintReport> lint_files(const std::vector<std::string>& paths);

    // Access effective severity for a rule
    Severity get_severity(const std::string& rule_id) const;

    // Access naming patterns
    const std::unordered_map<std::string, std::regex>& naming_patterns() const;

private:
    std::vector<std::unique_ptr<LintRule>> rules_;
    std::unordered_map<std::string, Severity> severity_overrides_;
    std::unordered_map<std::string, std::regex> naming_patterns_;

    void register_rules();
    Severity effective_severity(const LintRule& rule) const;
};

// ---------------------------------------------------------------------------
// Factory: create all built-in rules
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<LintRule>> create_all_rules();

// Severity conversion helpers
Severity severity_from_string(const std::string& s);
std::string severity_to_string(Severity s);

} // namespace loom::lint
