#pragma once

#include <loom/lang/token.hpp>
#include <string>
#include <vector>

namespace loom {

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class DesignUnitKind {
    Module,
    Package,
    Interface,
    Class,
    Program
};

enum class PortDirection {
    Input,
    Output,
    Inout,
    Ref
};

enum class AlwaysKind {
    Plain,     // always @(...)
    Star,      // always @(*)
    Comb,      // always_comb
    Ff,        // always_ff
    Latch      // always_latch
};

enum class CaseKind {
    Case,
    Casex,
    Casez
};

// ---------------------------------------------------------------------------
// Data structures extracted from design units
// ---------------------------------------------------------------------------

struct PortDecl {
    std::string name;
    PortDirection direction = PortDirection::Input;
    std::string type_text;   // e.g. "wire", "logic [7:0]", "reg [WIDTH-1:0]"
    SourcePos pos;
};

struct ParamDecl {
    std::string name;
    std::string default_text;  // raw text of default value expression
    bool is_localparam = false;
    SourcePos pos;
};

struct Instantiation {
    std::string module_name;    // instantiated module/interface name
    std::string instance_name;  // instance identifier
    bool is_parameterized = false;
    SourcePos pos;
};

struct Assignment {
    bool is_blocking = true;  // = vs <=
    std::string target;       // LHS identifier
    SourcePos pos;
};

struct AlwaysBlock {
    AlwaysKind kind = AlwaysKind::Plain;
    std::string label;
    std::vector<Assignment> assignments;
    SourcePos pos;
};

struct CaseStatement {
    CaseKind kind = CaseKind::Case;
    bool has_default = false;
    bool is_unique = false;
    bool is_priority = false;
    SourcePos pos;
};

struct SignalDecl {
    std::string name;
    std::string type_text;  // "wire", "reg", "logic", etc.
    SourcePos pos;
};

struct GenerateBlock {
    std::string label;
    bool has_label = false;
    SourcePos pos;
};

struct LabeledBlock {
    std::string begin_label;
    std::string end_label;
    bool labels_match = true;
    SourcePos pos;
};

struct ImportDecl {
    std::string package_name;
    std::string symbol;       // specific symbol or "*" for wildcard
    bool is_wildcard = false;
    SourcePos pos;
};

// ---------------------------------------------------------------------------
// Design unit: bundles all extracted data per module/package/interface/class
// ---------------------------------------------------------------------------

struct DesignUnit {
    DesignUnitKind kind = DesignUnitKind::Module;
    std::string name;
    int start_line = 0;
    int end_line = 0;
    int depth = 0;  // nesting depth (0 = top-level)

    std::vector<PortDecl> ports;
    std::vector<ParamDecl> params;
    std::vector<Instantiation> instantiations;
    std::vector<ImportDecl> imports;
    std::vector<AlwaysBlock> always_blocks;
    std::vector<CaseStatement> case_statements;
    std::vector<SignalDecl> signals;
    std::vector<GenerateBlock> generate_blocks;
    std::vector<LabeledBlock> labeled_blocks;
    bool has_defparam = false;

    SourcePos pos;
};

// ---------------------------------------------------------------------------
// Parse result
// ---------------------------------------------------------------------------

struct Diagnostic {
    std::string message;
    std::string file;
    int line = 0;
    int col = 0;
};

struct ParseResult {
    std::vector<DesignUnit> units;
    std::vector<Diagnostic> diagnostics;
};

} // namespace loom
