#include <loom/lint.hpp>
#include <loom/lang/parser.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace loom::lint {

// ===========================================================================
// Severity helpers
// ===========================================================================

Severity severity_from_string(const std::string& s) {
    if (s == "off")   return Severity::Off;
    if (s == "warn")  return Severity::Warn;
    if (s == "error") return Severity::Error;
    return Severity::Warn;  // default fallback
}

std::string severity_to_string(Severity s) {
    switch (s) {
        case Severity::Off:   return "off";
        case Severity::Warn:  return "warning";
        case Severity::Error: return "error";
    }
    return "warning";
}

// ===========================================================================
// LintDiagnostic
// ===========================================================================

std::string LintDiagnostic::format() const {
    std::ostringstream ss;
    ss << file << ":" << line << ":" << col << ": "
       << severity_to_string(severity) << ": [" << rule_id << "] " << message;
    return ss.str();
}

std::string LintDiagnostic::to_json() const {
    std::ostringstream ss;
    ss << "{\"severity\":\"" << severity_to_string(severity)
       << "\",\"rule_id\":\"" << rule_id
       << "\",\"message\":\"" << message
       << "\",\"file\":\"" << file
       << "\",\"line\":" << line
       << ",\"col\":" << col << "}";
    return ss.str();
}

// ===========================================================================
// LintReport
// ===========================================================================

std::string LintReport::to_json() const {
    std::ostringstream ss;
    ss << "{\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) ss << ",";
        ss << diagnostics[i].to_json();
    }
    ss << "],\"summary\":{\"files_checked\":" << files_checked
       << ",\"warnings\":" << warn_count
       << ",\"errors\":" << error_count << "}}";
    return ss.str();
}

// ===========================================================================
// SuppressionMap
// ===========================================================================

SuppressionMap SuppressionMap::build(const std::vector<Comment>& comments) {
    SuppressionMap map;
    for (auto& c : comments) {
        if (c.kind != CommentKind::Suppression) continue;
        int line = c.pos.line;
        // Suppress on same line and next line
        map.suppressions_[line].push_back(c.rule_id);
        map.suppressions_[line + 1].push_back(c.rule_id);
    }
    return map;
}

bool SuppressionMap::is_suppressed(const std::string& rule_id, int line) const {
    auto it = suppressions_.find(line);
    if (it == suppressions_.end()) return false;
    for (auto& rid : it->second) {
        if (rid == "*" || rid == rule_id) return true;
    }
    return false;
}

// ===========================================================================
// LintRule — default no-op implementations
// ===========================================================================

void LintRule::check(const DesignUnit&, const std::string&,
                     std::vector<LintDiagnostic>&) {}

void LintRule::check_file(const ParseResult&, const std::string&,
                          std::vector<LintDiagnostic>&) {}

void LintRule::check_tokens(const LexResult&, const std::string&,
                            std::vector<LintDiagnostic>&) {}

void LintRule::check_project(
    const std::vector<std::pair<std::string, ParseResult>>&,
    std::vector<LintDiagnostic>&) {}

void LintRule::emit(std::vector<LintDiagnostic>& out,
                    Severity sev,
                    const std::string& file,
                    int line, int col,
                    const std::string& msg) const {
    out.push_back({sev, id(), msg, file, line, col});
}

// ===========================================================================
// Correctness Rules
// ===========================================================================

// blocking-in-ff: blocking assignments in always_ff
class BlockingInFfRule : public LintRule {
public:
    std::string id() const override { return "blocking-in-ff"; }
    std::string description() const override {
        return "Blocking assignment in always_ff block";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Error; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& ab : unit.always_blocks) {
            if (ab.kind != AlwaysKind::Ff) continue;
            for (auto& a : ab.assignments) {
                if (a.is_blocking) {
                    emit(out, Severity::Error, filename, a.pos.line, a.pos.col,
                         "blocking assignment '=' in always_ff block");
                }
            }
        }
    }
};

// nonblocking-in-comb: non-blocking assignments in always_comb
class NonblockingInCombRule : public LintRule {
public:
    std::string id() const override { return "nonblocking-in-comb"; }
    std::string description() const override {
        return "Non-blocking assignment in always_comb block";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Error; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& ab : unit.always_blocks) {
            if (ab.kind != AlwaysKind::Comb) continue;
            for (auto& a : ab.assignments) {
                if (!a.is_blocking) {
                    emit(out, Severity::Error, filename, a.pos.line, a.pos.col,
                         "non-blocking assignment '<=' in always_comb block");
                }
            }
        }
    }
};

// mixed-blocking: both = and <= in same always block
class MixedBlockingRule : public LintRule {
public:
    std::string id() const override { return "mixed-blocking"; }
    std::string description() const override {
        return "Mixed blocking and non-blocking assignments in same always block";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Error; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& ab : unit.always_blocks) {
            bool has_blocking = false;
            bool has_nonblocking = false;
            for (auto& a : ab.assignments) {
                if (a.is_blocking) has_blocking = true;
                else has_nonblocking = true;
            }
            if (has_blocking && has_nonblocking) {
                emit(out, Severity::Error, filename, ab.pos.line, ab.pos.col,
                     "mixed blocking and non-blocking assignments in always block");
            }
        }
    }
};

// case-missing-default: case without default (unless unique/priority)
class CaseMissingDefaultRule : public LintRule {
public:
    std::string id() const override { return "case-missing-default"; }
    std::string description() const override {
        return "Case statement without default branch";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& cs : unit.case_statements) {
            if (!cs.has_default && !cs.is_unique && !cs.is_priority) {
                emit(out, Severity::Warn, filename, cs.pos.line, cs.pos.col,
                     "case statement without default branch");
            }
        }
    }
};

// casex-usage: casex is error-prone, prefer casez or case inside
class CasexUsageRule : public LintRule {
public:
    std::string id() const override { return "casex-usage"; }
    std::string description() const override {
        return "Use of casex (prefer casez or case inside)";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& cs : unit.case_statements) {
            if (cs.kind == CaseKind::Casex) {
                emit(out, Severity::Warn, filename, cs.pos.line, cs.pos.col,
                     "casex is error-prone; prefer casez or case inside");
            }
        }
    }
};

// always-star: always @(*) — suggest always_comb
class AlwaysStarRule : public LintRule {
public:
    std::string id() const override { return "always-star"; }
    std::string description() const override {
        return "Use of always @(*) — prefer always_comb";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& ab : unit.always_blocks) {
            if (ab.kind == AlwaysKind::Star) {
                emit(out, Severity::Warn, filename, ab.pos.line, ab.pos.col,
                     "always @(*) is Verilog-2001; prefer always_comb");
            }
        }
    }
};

// defparam-usage: defparam is deprecated
class DefparamUsageRule : public LintRule {
public:
    std::string id() const override { return "defparam-usage"; }
    std::string description() const override {
        return "Use of defparam (deprecated)";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        if (unit.has_defparam) {
            emit(out, Severity::Warn, filename, unit.pos.line, unit.pos.col,
                 "defparam is deprecated; use parameter overrides");
        }
    }
};

// implicit-net: no `default_nettype none directive
class ImplicitNetRule : public LintRule {
public:
    std::string id() const override { return "implicit-net"; }
    std::string description() const override {
        return "No `default_nettype none directive";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }

    void check_tokens(const LexResult& lex_result, const std::string& filename,
                      std::vector<LintDiagnostic>& out) override {
        bool found = false;
        for (auto& tok : lex_result.tokens) {
            if (tok.type == VerilogTokenType::Directive) {
                // Directive text includes the backtick, e.g. "`default_nettype"
                if (tok.text.find("default_nettype") != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            emit(out, Severity::Warn, filename, 1, 1,
                 "no `default_nettype none directive found");
        }
    }
};

// ===========================================================================
// Structure Rules
// ===========================================================================

// label-mismatch: begin/end label mismatch
class LabelMismatchRule : public LintRule {
public:
    std::string id() const override { return "label-mismatch"; }
    std::string description() const override {
        return "Begin/end label mismatch";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& lb : unit.labeled_blocks) {
            if (!lb.labels_match) {
                emit(out, Severity::Warn, filename, lb.pos.line, lb.pos.col,
                     "begin label '" + lb.begin_label +
                     "' does not match end label '" + lb.end_label + "'");
            }
        }
    }
};

// unlabeled-generate: generate block without a label
class UnlabeledGenerateRule : public LintRule {
public:
    std::string id() const override { return "unlabeled-generate"; }
    std::string description() const override {
        return "Generate block without a label";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        for (auto& gb : unit.generate_blocks) {
            if (!gb.has_label) {
                emit(out, Severity::Warn, filename, gb.pos.line, gb.pos.col,
                     "generate block without a label");
            }
        }
    }
};

// one-module-per-file: multiple top-level design units in one file
class OneModulePerFileRule : public LintRule {
public:
    std::string id() const override { return "one-module-per-file"; }
    std::string description() const override {
        return "Multiple top-level design units in a single file";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Off; }

    void check_file(const ParseResult& parse_result, const std::string& filename,
                    std::vector<LintDiagnostic>& out) override {
        int top_level_count = 0;
        for (auto& unit : parse_result.units) {
            if (unit.depth == 0) ++top_level_count;
        }
        if (top_level_count > 1) {
            emit(out, Severity::Warn, filename, 1, 1,
                 "file contains " + std::to_string(top_level_count) +
                 " top-level design units (expected 1)");
        }
    }
};

// module-filename-match: module name should match file stem
class ModuleFilenameMatchRule : public LintRule {
public:
    std::string id() const override { return "module-filename-match"; }
    std::string description() const override {
        return "Module name does not match filename";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Off; }

    void check_file(const ParseResult& parse_result, const std::string& filename,
                    std::vector<LintDiagnostic>& out) override {
        std::string stem = fs::path(filename).stem().string();
        for (auto& unit : parse_result.units) {
            if (unit.depth != 0) continue;
            if (unit.kind != DesignUnitKind::Module) continue;
            if (unit.name != stem) {
                emit(out, Severity::Warn, filename, unit.pos.line, unit.pos.col,
                     "module '" + unit.name +
                     "' does not match filename '" + stem + "'");
            }
        }
    }
};

// ifdef-balance: unmatched `ifdef/`endif
class IfdefBalanceRule : public LintRule {
public:
    std::string id() const override { return "ifdef-balance"; }
    std::string description() const override {
        return "Unbalanced `ifdef/`endif directives";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Error; }

    void check_tokens(const LexResult& lex_result, const std::string& filename,
                      std::vector<LintDiagnostic>& out) override {
        struct IfdefInfo { int line; int col; };
        std::vector<IfdefInfo> stack;

        for (auto& tok : lex_result.tokens) {
            if (tok.type != VerilogTokenType::Directive) continue;
            const auto& t = tok.text;
            if (t == "`ifdef" || t == "`ifndef") {
                stack.push_back({tok.pos.line, tok.pos.col});
            } else if (t == "`endif") {
                if (stack.empty()) {
                    emit(out, Severity::Error, filename,
                         tok.pos.line, tok.pos.col,
                         "`endif without matching `ifdef/`ifndef");
                } else {
                    stack.pop_back();
                }
            }
        }
        for (auto& info : stack) {
            emit(out, Severity::Error, filename, info.line, info.col,
                 "`ifdef/`ifndef without matching `endif");
        }
    }
};

// unused-signal: signal declared but never assigned and not an output port
class UnusedSignalRule : public LintRule {
public:
    std::string id() const override { return "unused-signal"; }
    std::string description() const override {
        return "Signal declared but never referenced in assignments";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        // Collect all signal names referenced in assignments (both targets and ports)
        std::unordered_set<std::string> referenced;
        for (auto& ab : unit.always_blocks) {
            for (auto& a : ab.assignments) {
                referenced.insert(a.target);
            }
        }
        // Port names are considered "used"
        for (auto& p : unit.ports) {
            referenced.insert(p.name);
        }
        // Params are used
        for (auto& p : unit.params) {
            referenced.insert(p.name);
        }
        // Instantiation connections implicitly use signals (approximate)
        // For now, if there are instantiations, skip signal analysis
        // (we can't see which signals are connected to ports)
        if (!unit.instantiations.empty()) return;

        for (auto& sig : unit.signals) {
            if (referenced.find(sig.name) == referenced.end()) {
                emit(out, Severity::Warn, filename, sig.pos.line, sig.pos.col,
                     "signal '" + sig.name + "' is declared but never referenced");
            }
        }
    }
};

// undriven-signal: declared signal never appears as assignment target, not input port
class UndrivenSignalRule : public LintRule {
public:
    std::string id() const override { return "undriven-signal"; }
    std::string description() const override {
        return "Signal declared but never driven";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Warn; }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        // Collect all assignment targets
        std::unordered_set<std::string> driven;
        for (auto& ab : unit.always_blocks) {
            for (auto& a : ab.assignments) {
                driven.insert(a.target);
            }
        }
        // Input ports are driven externally
        std::unordered_set<std::string> input_ports;
        for (auto& p : unit.ports) {
            if (p.direction == PortDirection::Input ||
                p.direction == PortDirection::Inout) {
                input_ports.insert(p.name);
            }
        }
        // If there are instantiations, skip (outputs of instances drive signals)
        if (!unit.instantiations.empty()) return;

        for (auto& sig : unit.signals) {
            if (driven.find(sig.name) == driven.end() &&
                input_ports.find(sig.name) == input_ports.end()) {
                emit(out, Severity::Warn, filename, sig.pos.line, sig.pos.col,
                     "signal '" + sig.name + "' is declared but never driven");
            }
        }
    }
};

// ===========================================================================
// Style Rules
// ===========================================================================

// Base class for naming convention rules
class NamingRule : public LintRule {
public:
    RuleCategory category() const override { return RuleCategory::Style; }
    Severity default_severity() const override { return Severity::Off; }

    void set_pattern(const std::string& pattern) {
        if (!pattern.empty()) {
            pattern_str_ = pattern;
            pattern_ = std::regex(pattern);
            has_pattern_ = true;
        }
    }

    bool has_pattern() const { return has_pattern_; }

protected:
    bool matches(const std::string& name) const {
        if (!has_pattern_) return true;
        return std::regex_match(name, pattern_);
    }

    std::string pattern_str_;

private:
    std::regex pattern_;
    bool has_pattern_ = false;
};

// naming-module
class NamingModuleRule : public NamingRule {
public:
    std::string id() const override { return "naming-module"; }
    std::string description() const override {
        return "Module name does not match naming convention";
    }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        if (!has_pattern()) return;
        if (unit.kind != DesignUnitKind::Module) return;
        if (!matches(unit.name)) {
            emit(out, Severity::Warn, filename, unit.pos.line, unit.pos.col,
                 "module '" + unit.name +
                 "' does not match pattern '" + pattern_str_ + "'");
        }
    }
};

// naming-signal
class NamingSignalRule : public NamingRule {
public:
    std::string id() const override { return "naming-signal"; }
    std::string description() const override {
        return "Signal name does not match naming convention";
    }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        if (!has_pattern()) return;
        for (auto& sig : unit.signals) {
            if (!matches(sig.name)) {
                emit(out, Severity::Warn, filename,
                     sig.pos.line, sig.pos.col,
                     "signal '" + sig.name +
                     "' does not match pattern '" + pattern_str_ + "'");
            }
        }
    }
};

// naming-parameter
class NamingParameterRule : public NamingRule {
public:
    std::string id() const override { return "naming-parameter"; }
    std::string description() const override {
        return "Parameter name does not match naming convention";
    }

    void check(const DesignUnit& unit, const std::string& filename,
               std::vector<LintDiagnostic>& out) override {
        if (!has_pattern()) return;
        for (auto& p : unit.params) {
            if (!matches(p.name)) {
                emit(out, Severity::Warn, filename,
                     p.pos.line, p.pos.col,
                     "parameter '" + p.name +
                     "' does not match pattern '" + pattern_str_ + "'");
            }
        }
    }
};

// ===========================================================================
// Project-level Rules
// ===========================================================================

// duplicate-module: same module name defined in multiple files
class DuplicateModuleRule : public LintRule {
public:
    std::string id() const override { return "duplicate-module"; }
    std::string description() const override {
        return "Same module defined in multiple files";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Error; }

    void check_project(
        const std::vector<std::pair<std::string, ParseResult>>& all_files,
        std::vector<LintDiagnostic>& out) override {

        // module_name -> first file
        std::unordered_map<std::string, std::string> seen;
        for (auto& [filename, pr] : all_files) {
            for (auto& unit : pr.units) {
                if (unit.depth != 0) continue;
                auto it = seen.find(unit.name);
                if (it != seen.end()) {
                    emit(out, Severity::Error, filename,
                         unit.pos.line, unit.pos.col,
                         "module '" + unit.name +
                         "' already defined in " + it->second);
                } else {
                    seen[unit.name] = filename;
                }
            }
        }
    }
};

// ===========================================================================
// Deferred stub rules (no-op)
// ===========================================================================

class EmptyPortConnectionRule : public LintRule {
public:
    std::string id() const override { return "empty-port-connection"; }
    std::string description() const override {
        return "Empty port connection in instantiation (deferred)";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }
};

class MissingPortConnectionRule : public LintRule {
public:
    std::string id() const override { return "missing-port-connection"; }
    std::string description() const override {
        return "Missing port connection in instantiation (deferred)";
    }
    RuleCategory category() const override { return RuleCategory::Correctness; }
    Severity default_severity() const override { return Severity::Warn; }
};

class MissingBeginEndRule : public LintRule {
public:
    std::string id() const override { return "missing-begin-end"; }
    std::string description() const override {
        return "Missing begin/end around multi-line block (deferred)";
    }
    RuleCategory category() const override { return RuleCategory::Structure; }
    Severity default_severity() const override { return Severity::Warn; }
};

// ===========================================================================
// Factory
// ===========================================================================

std::vector<std::unique_ptr<LintRule>> create_all_rules() {
    std::vector<std::unique_ptr<LintRule>> rules;
    // Correctness
    rules.push_back(std::make_unique<BlockingInFfRule>());
    rules.push_back(std::make_unique<NonblockingInCombRule>());
    rules.push_back(std::make_unique<MixedBlockingRule>());
    rules.push_back(std::make_unique<CaseMissingDefaultRule>());
    rules.push_back(std::make_unique<CasexUsageRule>());
    rules.push_back(std::make_unique<AlwaysStarRule>());
    rules.push_back(std::make_unique<DefparamUsageRule>());
    rules.push_back(std::make_unique<ImplicitNetRule>());
    rules.push_back(std::make_unique<DuplicateModuleRule>());
    // Deferred correctness
    rules.push_back(std::make_unique<EmptyPortConnectionRule>());
    rules.push_back(std::make_unique<MissingPortConnectionRule>());
    // Structure
    rules.push_back(std::make_unique<LabelMismatchRule>());
    rules.push_back(std::make_unique<UnlabeledGenerateRule>());
    rules.push_back(std::make_unique<IfdefBalanceRule>());
    rules.push_back(std::make_unique<OneModulePerFileRule>());
    rules.push_back(std::make_unique<ModuleFilenameMatchRule>());
    rules.push_back(std::make_unique<UnusedSignalRule>());
    rules.push_back(std::make_unique<UndrivenSignalRule>());
    // Deferred structure
    rules.push_back(std::make_unique<MissingBeginEndRule>());
    // Style
    rules.push_back(std::make_unique<NamingModuleRule>());
    rules.push_back(std::make_unique<NamingSignalRule>());
    rules.push_back(std::make_unique<NamingParameterRule>());
    return rules;
}

// ===========================================================================
// LintEngine
// ===========================================================================

LintEngine::LintEngine() {
    register_rules();
}

void LintEngine::register_rules() {
    rules_ = create_all_rules();
}

void LintEngine::configure(const LintConfig& config) {
    // Apply severity overrides
    for (auto& [rule_id, sev_str] : config.rules) {
        severity_overrides_[rule_id] = severity_from_string(sev_str);
    }

    // Apply naming patterns
    for (auto& [key, pattern] : config.naming) {
        std::string rule_id = "naming-" + key;
        if (!pattern.empty()) {
            naming_patterns_[rule_id] = std::regex(pattern);
        }
        // Also set the pattern on the rule itself
        for (auto& rule : rules_) {
            if (rule->id() == rule_id) {
                auto* nr = dynamic_cast<NamingRule*>(rule.get());
                if (nr) nr->set_pattern(pattern);
            }
        }
    }
}

Severity LintEngine::get_severity(const std::string& rule_id) const {
    auto it = severity_overrides_.find(rule_id);
    if (it != severity_overrides_.end()) return it->second;
    for (auto& rule : rules_) {
        if (rule->id() == rule_id) return rule->default_severity();
    }
    return Severity::Off;
}

Severity LintEngine::effective_severity(const LintRule& rule) const {
    auto it = severity_overrides_.find(rule.id());
    if (it != severity_overrides_.end()) return it->second;
    return rule.default_severity();
}

const std::unordered_map<std::string, std::regex>& LintEngine::naming_patterns() const {
    return naming_patterns_;
}

LintReport LintEngine::lint_parsed(const LexResult& lex_result,
                                   const ParseResult& parse_result,
                                   const std::string& filename) {
    LintReport report;
    report.files_checked = 1;

    // Build suppression map
    auto suppressions = SuppressionMap::build(lex_result.comments);

    // Collect raw diagnostics
    std::vector<LintDiagnostic> raw;

    for (auto& rule : rules_) {
        Severity sev = effective_severity(*rule);
        if (sev == Severity::Off) continue;

        // Per-unit checks
        for (auto& unit : parse_result.units) {
            rule->check(unit, filename, raw);
        }

        // Per-file checks
        rule->check_file(parse_result, filename, raw);

        // Token-level checks
        rule->check_tokens(lex_result, filename, raw);
    }

    // Filter by suppression and apply effective severity
    for (auto& d : raw) {
        if (suppressions.is_suppressed(d.rule_id, d.line)) continue;

        // Override severity to effective severity
        Severity eff = get_severity(d.rule_id);
        if (eff == Severity::Off) continue;
        d.severity = eff;

        if (d.severity == Severity::Warn) ++report.warn_count;
        else if (d.severity == Severity::Error) ++report.error_count;
        report.diagnostics.push_back(std::move(d));
    }

    return report;
}

Result<LintReport> LintEngine::lint_file(const std::string& path) {
    // Read file
    std::ifstream file(path);
    if (!file.is_open()) {
        return LoomError(LoomError::IO, "cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    // Determine if SystemVerilog
    std::string ext = fs::path(path).extension().string();
    bool is_sv = (ext == ".sv");

    // Lex
    auto lex_result = loom::lex(source, path, is_sv);
    LOOM_TRY(lex_result);

    // Parse
    auto parse_result = loom::parse(lex_result.value(), path, is_sv);
    LOOM_TRY(parse_result);

    return Result<LintReport>::ok(
        lint_parsed(lex_result.value(), parse_result.value(), path));
}

Result<LintReport> LintEngine::lint_files(const std::vector<std::string>& paths) {
    LintReport combined;

    // Collect all parsed files for project-level rules
    std::vector<std::pair<std::string, ParseResult>> all_parsed;

    for (auto& path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return LoomError(LoomError::IO, "cannot open file: " + path);
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        std::string ext = fs::path(path).extension().string();
        bool is_sv = (ext == ".sv");

        auto lex_result = loom::lex(source, path, is_sv);
        LOOM_TRY(lex_result);

        auto parse_result = loom::parse(lex_result.value(), path, is_sv);
        LOOM_TRY(parse_result);

        // Single-file lint
        auto report = lint_parsed(lex_result.value(), parse_result.value(), path);
        for (auto& d : report.diagnostics) {
            combined.diagnostics.push_back(std::move(d));
        }
        combined.warn_count += report.warn_count;
        combined.error_count += report.error_count;

        all_parsed.emplace_back(path, std::move(parse_result.value()));
    }

    combined.files_checked = static_cast<int>(paths.size());

    // Project-level rules
    std::vector<LintDiagnostic> project_diags;
    for (auto& rule : rules_) {
        Severity sev = effective_severity(*rule);
        if (sev == Severity::Off) continue;
        rule->check_project(all_parsed, project_diags);
    }

    for (auto& d : project_diags) {
        Severity eff = get_severity(d.rule_id);
        if (eff == Severity::Off) continue;
        d.severity = eff;
        if (d.severity == Severity::Warn) ++combined.warn_count;
        else if (d.severity == Severity::Error) ++combined.error_count;
        combined.diagnostics.push_back(std::move(d));
    }

    return Result<LintReport>::ok(std::move(combined));
}

} // namespace loom::lint
