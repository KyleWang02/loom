// demo_lint.cpp — Comprehensive lint engine demonstration
//
// Exercises all 19 active rules across 4 check levels:
//   - Per-unit correctness (blocking-in-ff, nonblocking-in-comb, mixed-blocking,
//     case-missing-default, casex-usage, always-star, defparam-usage)
//   - Per-unit structure (label-mismatch, unlabeled-generate, unused-signal, undriven-signal)
//   - Token-level (implicit-net, ifdef-balance)
//   - Per-file structure (one-module-per-file, module-filename-match)
//   - Style naming rules (naming-module, naming-signal, naming-parameter)
//   - Project-level (duplicate-module across files)
//   - Suppression with // loom: ignore[rule-id] and wildcard *
//   - Configuration overrides (promote warn→error, disable rules, enable off-by-default)
//   - Output in both GCC-compatible and JSON formats

#include <loom/lint.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <loom/log.hpp>
#include <loom/manifest.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using namespace loom;
using namespace loom::lint;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void print_header(const char* title) {
    std::cout << "\n"
              << "========================================================\n"
              << "  " << title << "\n"
              << "========================================================\n\n";
}

static void print_section(const char* title) {
    std::cout << "\n--- " << title << " ---\n\n";
}

static void print_report(const LintReport& report) {
    for (auto& d : report.diagnostics) {
        std::cout << "  " << d.format() << "\n";
    }
    std::cout << "\n  Summary: " << report.warn_count << " warnings, "
              << report.error_count << " errors, "
              << report.files_checked << " files checked\n";
}

// Lex+parse helper
static std::pair<LexResult, ParseResult> do_lex_parse(
    const std::string& source, const std::string& filename, bool is_sv = true) {
    auto lr = lex(source, filename, is_sv);
    if (lr.is_err()) {
        std::cerr << "Lex error: " << lr.error().format() << "\n";
        std::exit(1);
    }
    auto pr = parse(lr.value(), filename, is_sv);
    if (pr.is_err()) {
        std::cerr << "Parse error: " << pr.error().format() << "\n";
        std::exit(1);
    }
    return {std::move(lr.value()), std::move(pr.value())};
}

// Write a temp file
struct TempDir {
    fs::path path;
    TempDir() : path(fs::temp_directory_path() / "loom_demo_lint") {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p.string();
    }
};

// ===========================================================================
// Demo scenarios
// ===========================================================================

static void demo_correctness_rules() {
    print_header("1. CORRECTNESS RULES (7 rules)");

    const char* source = R"(
module buggy_design(
  input  logic clk,
  input  logic rst_n,
  input  logic [7:0] data_in,
  output logic [7:0] data_out,
  output logic valid
);
  logic [7:0] temp;
  logic flag;

  // BUG: blocking assignment in always_ff
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      data_out = 8'h0;
    else
      data_out = data_in;
  end

  // BUG: non-blocking assignment in always_comb
  always_comb begin
    temp <= data_in & 8'hFF;
  end

  // BUG: mixed blocking and non-blocking
  always @(posedge clk) begin
    flag = 1'b1;
    valid <= flag;
  end

  // BUG: case without default
  always_comb begin
    case (data_in[1:0])
      2'b00: temp = 8'h0;
      2'b01: temp = 8'h1;
    endcase
  end

  // BUG: casex usage
  always_comb begin
    casex (data_in[1:0])
      2'b1?: temp = 8'hFF;
      default: temp = 8'h0;
    endcase
  end

  // BUG: always @(*) instead of always_comb
  always @(*) begin
    flag = |data_in;
  end

  // BUG: defparam usage
  sub_module u1();
  defparam u1.WIDTH = 16;
endmodule
)";

    auto [lr, pr] = do_lex_parse(source, "buggy_design.sv");
    LintEngine engine;
    auto report = engine.lint_parsed(lr, pr, "buggy_design.sv");

    std::cout << "Source: buggy_design.sv (intentionally buggy module)\n";
    std::cout << "Expected: 7 different correctness rule violations\n\n";
    print_report(report);

    // Verify we hit all 7
    std::cout << "\n  Rules triggered:\n";
    std::set<std::string> seen;
    for (auto& d : report.diagnostics) {
        if (d.rule_id.find("blocking-in-ff") != std::string::npos ||
            d.rule_id.find("nonblocking-in-comb") != std::string::npos ||
            d.rule_id.find("mixed-blocking") != std::string::npos ||
            d.rule_id.find("case-missing-default") != std::string::npos ||
            d.rule_id.find("casex-usage") != std::string::npos ||
            d.rule_id.find("always-star") != std::string::npos ||
            d.rule_id.find("defparam-usage") != std::string::npos) {
            seen.insert(d.rule_id);
        }
    }
    for (auto& r : seen) std::cout << "    [x] " << r << "\n";
    std::cout << "  Total correctness rules fired: " << seen.size() << "/7\n";
}

static void demo_structure_rules() {
    print_header("2. STRUCTURE RULES (4 per-unit rules)");

    const char* source = R"(
module struct_issues;
  // BUG: label mismatch
  initial begin : block_alpha
    $display("hello");
  end : block_beta

  // BUG: unlabeled generate
  genvar i;
  generate
    for (i = 0; i < 4; i = i + 1) begin
    end
  endgenerate

  // BUG: unused signal (never referenced)
  logic unused_wire;

  // BUG: undriven signal (declared but never driven)
  logic undriven_bus;

endmodule
)";

    auto [lr, pr] = do_lex_parse(source, "struct_issues.sv");
    LintEngine engine;
    auto report = engine.lint_parsed(lr, pr, "struct_issues.sv");

    std::cout << "Source: struct_issues.sv (structural problems)\n";
    std::cout << "Expected: label-mismatch, unlabeled-generate, unused-signal, undriven-signal\n\n";
    print_report(report);
}

static void demo_token_level_rules() {
    print_header("3. TOKEN-LEVEL RULES (implicit-net, ifdef-balance)");

    print_section("3a. Missing `default_nettype none");
    {
        const char* source = R"(
module no_default_nettype;
  wire a;
endmodule
)";
        auto [lr, pr] = do_lex_parse(source, "no_default.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "no_default.sv");
        std::cout << "Source: no_default.sv (no `default_nettype none)\n\n";
        print_report(report);
    }

    print_section("3b. Unbalanced `ifdef/`endif");
    {
        const char* source = R"(
`ifdef FEATURE_A
module guarded;
endmodule
// Missing `endif!
)";
        auto [lr, pr] = do_lex_parse(source, "bad_ifdef.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "bad_ifdef.sv");
        std::cout << "Source: bad_ifdef.sv (unmatched `ifdef)\n\n";
        print_report(report);
    }

    print_section("3c. Clean file — balanced ifdefs + default_nettype");
    {
        const char* source = R"(
`default_nettype none
`ifdef FEATURE_A
module clean_guarded;
endmodule
`endif
)";
        auto [lr, pr] = do_lex_parse(source, "clean.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "clean.sv");
        std::cout << "Source: clean.sv (no violations expected)\n\n";
        print_report(report);
    }
}

static void demo_style_naming_rules() {
    print_header("4. STYLE NAMING RULES (naming-module, naming-signal, naming-parameter)");

    const char* source = R"(
`default_nettype none
module BadModuleName;
  logic CapitalSignal;
  parameter lowercase_param = 42;
endmodule
)";

    LintConfig config;
    // Enable naming rules with patterns
    config.rules["naming-module"] = "warn";
    config.rules["naming-signal"] = "warn";
    config.rules["naming-parameter"] = "warn";
    config.naming["module"] = "[a-z][a-z0-9_]*";       // snake_case modules
    config.naming["signal"] = "[a-z][a-z0-9_]*";       // snake_case signals
    config.naming["parameter"] = "[A-Z][A-Z0-9_]*";    // UPPER_CASE parameters

    auto [lr, pr] = do_lex_parse(source, "bad_names.sv");
    LintEngine engine;
    engine.configure(config);
    auto report = engine.lint_parsed(lr, pr, "bad_names.sv");

    std::cout << "Source: bad_names.sv with naming conventions:\n";
    std::cout << "  module  pattern: [a-z][a-z0-9_]* (snake_case)\n";
    std::cout << "  signal  pattern: [a-z][a-z0-9_]* (snake_case)\n";
    std::cout << "  parameter pattern: [A-Z][A-Z0-9_]* (UPPER_CASE)\n\n";
    print_report(report);
}

static void demo_per_file_rules() {
    print_header("5. PER-FILE RULES (one-module-per-file, module-filename-match)");

    print_section("5a. Multiple modules in one file");
    {
        const char* source = R"(
`default_nettype none
module alpha; endmodule
module beta; endmodule
module gamma; endmodule
)";
        LintConfig config;
        config.rules["one-module-per-file"] = "warn";
        config.rules["module-filename-match"] = "warn";

        auto [lr, pr] = do_lex_parse(source, "multi.sv");
        LintEngine engine;
        engine.configure(config);
        auto report = engine.lint_parsed(lr, pr, "multi.sv");

        std::cout << "Source: multi.sv (3 modules in 1 file, name mismatch)\n\n";
        print_report(report);
    }

    print_section("5b. Clean single-module file with matching name");
    {
        const char* source = R"(
`default_nettype none
module clean_file; endmodule
)";
        LintConfig config;
        config.rules["one-module-per-file"] = "warn";
        config.rules["module-filename-match"] = "warn";

        auto [lr, pr] = do_lex_parse(source, "clean_file.sv");
        LintEngine engine;
        engine.configure(config);
        auto report = engine.lint_parsed(lr, pr, "clean_file.sv");

        std::cout << "Source: clean_file.sv (single module, name matches)\n\n";
        print_report(report);
    }
}

static void demo_suppression() {
    print_header("6. SUPPRESSION (// loom: ignore[rule-id])");

    print_section("6a. Targeted suppression");
    {
        const char* source = R"(
`default_nettype none
module suppressed;
  logic clk, d, q;
  always_ff @(posedge clk)
    q = d; // loom: ignore[blocking-in-ff]
endmodule
)";
        auto [lr, pr] = do_lex_parse(source, "suppressed.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "suppressed.sv");

        std::cout << "Source: suppressed.sv\n";
        std::cout << "  Line has: q = d; // loom: ignore[blocking-in-ff]\n";
        std::cout << "  Expected: blocking-in-ff suppressed\n\n";
        print_report(report);
    }

    print_section("6b. Wildcard suppression");
    {
        const char* source = R"(
`default_nettype none
module wildcard;
  logic clk, d, q;
  always_ff @(posedge clk)
    q = d; // loom: ignore[*]
endmodule
)";
        auto [lr, pr] = do_lex_parse(source, "wildcard.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "wildcard.sv");

        std::cout << "Source: wildcard.sv\n";
        std::cout << "  Line has: q = d; // loom: ignore[*]\n";
        std::cout << "  Expected: ALL rules suppressed on that line\n\n";
        print_report(report);
    }
}

static void demo_config_overrides() {
    print_header("7. CONFIGURATION OVERRIDES");

    const char* source = R"(
module override_demo;
  reg a, b, c;
  always @(*) begin
    c = a & b;
  end

  always_comb begin
    case (a)
      1'b0: b = 1'b0;
    endcase
  end
endmodule
)";

    print_section("7a. Default severities");
    {
        auto [lr, pr] = do_lex_parse(source, "demo.sv");
        LintEngine engine;
        auto report = engine.lint_parsed(lr, pr, "demo.sv");
        std::cout << "Default config:\n";
        print_report(report);
    }

    print_section("7b. Promote always-star to error");
    {
        LintConfig config;
        config.rules["always-star"] = "error";

        auto [lr, pr] = do_lex_parse(source, "demo.sv");
        LintEngine engine;
        engine.configure(config);
        auto report = engine.lint_parsed(lr, pr, "demo.sv");
        std::cout << "Config: always-star = error\n";
        print_report(report);
    }

    print_section("7c. Disable all except blocking rules");
    {
        LintConfig config;
        config.rules["always-star"] = "off";
        config.rules["case-missing-default"] = "off";
        config.rules["implicit-net"] = "off";
        config.rules["unused-signal"] = "off";
        config.rules["undriven-signal"] = "off";

        auto [lr, pr] = do_lex_parse(source, "demo.sv");
        LintEngine engine;
        engine.configure(config);
        auto report = engine.lint_parsed(lr, pr, "demo.sv");
        std::cout << "Config: most rules disabled\n";
        print_report(report);
    }
}

static void demo_project_level_rules() {
    print_header("8. PROJECT-LEVEL RULES (duplicate-module across files)");

    TempDir tmp;
    auto f1 = tmp.write("uart_tx.sv", R"(
`default_nettype none
module uart_tx;
endmodule
)");
    auto f2 = tmp.write("uart_tx_alt.sv", R"(
`default_nettype none
module uart_tx;
endmodule
)");
    auto f3 = tmp.write("uart_rx.sv", R"(
`default_nettype none
module uart_rx;
endmodule
)");

    LintEngine engine;
    auto result = engine.lint_files({f1, f2, f3});
    if (result.is_err()) {
        std::cerr << "Error: " << result.error().format() << "\n";
        return;
    }

    std::cout << "Files: uart_tx.sv, uart_tx_alt.sv, uart_rx.sv\n";
    std::cout << "  uart_tx defined in BOTH uart_tx.sv and uart_tx_alt.sv\n\n";
    print_report(result.value());
}

static void demo_json_output() {
    print_header("9. JSON OUTPUT FORMAT");

    const char* source = R"(
module json_demo;
  logic clk, d, q;
  always_ff @(posedge clk)
    q = d;
endmodule
)";

    auto [lr, pr] = do_lex_parse(source, "json_demo.sv");
    LintEngine engine;
    auto report = engine.lint_parsed(lr, pr, "json_demo.sv");

    std::cout << "Individual diagnostics:\n";
    for (auto& d : report.diagnostics) {
        std::cout << "  " << d.to_json() << "\n";
    }
    std::cout << "\nFull report:\n";
    std::cout << "  " << report.to_json() << "\n";
}

static void demo_clean_code() {
    print_header("10. CLEAN CODE — ZERO VIOLATIONS");

    const char* source = R"(
`default_nettype none
module clean_design #(
  parameter WIDTH = 8
)(
  input  logic             clk,
  input  logic             rst_n,
  input  logic [WIDTH-1:0] data_in,
  output logic [WIDTH-1:0] data_out
);
  logic [WIDTH-1:0] data_reg;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      data_reg <= '0;
    else
      data_reg <= data_in;
  end

  always_comb begin
    data_out = data_reg;
  end
endmodule
)";

    auto [lr, pr] = do_lex_parse(source, "clean_design.sv");
    LintEngine engine;
    auto report = engine.lint_parsed(lr, pr, "clean_design.sv");

    std::cout << "Source: clean_design.sv (proper SV design)\n";
    std::cout << "  - `default_nettype none present\n";
    std::cout << "  - Non-blocking in always_ff, blocking in always_comb\n";
    std::cout << "  - All signals driven and used via ports\n\n";
    print_report(report);
}

static void demo_all_rules_listing() {
    print_header("APPENDIX: ALL 22 REGISTERED RULES");

    auto rules = create_all_rules();
    std::cout << "  " << rules.size() << " rules total:\n\n";

    auto cat_str = [](RuleCategory c) -> const char* {
        switch (c) {
            case RuleCategory::Correctness: return "Correctness";
            case RuleCategory::Structure:   return "Structure";
            case RuleCategory::Style:       return "Style";
        }
        return "?";
    };

    std::cout << "  | # | Rule ID                    | Category      | Default  |\n";
    std::cout << "  |---|----------------------------|---------------|----------|\n";
    int n = 1;
    for (auto& r : rules) {
        std::string sev = severity_to_string(r->default_severity());
        if (r->default_severity() == Severity::Off) sev = "off";
        printf("  | %2d | %-26s | %-13s | %-8s |\n",
               n++, r->id().c_str(), cat_str(r->category()), sev.c_str());
    }
    std::cout << "\n";
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
    log::set_level(log::Warn);  // suppress info noise

    std::cout << "============================================================\n";
    std::cout << "     Loom Lint Engine — Comprehensive Demo\n";
    std::cout << "     22 rules, 4 check levels, suppression, JSON output\n";
    std::cout << "============================================================\n";

    demo_all_rules_listing();
    demo_correctness_rules();
    demo_structure_rules();
    demo_token_level_rules();
    demo_style_naming_rules();
    demo_per_file_rules();
    demo_suppression();
    demo_config_overrides();
    demo_project_level_rules();
    demo_json_output();
    demo_clean_code();

    std::cout << "\n============================================================\n";
    std::cout << "     Demo complete. All lint features exercised.\n";
    std::cout << "============================================================\n\n";

    return 0;
}
