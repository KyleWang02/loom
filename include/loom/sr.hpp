#pragma once

#include <loom/result.hpp>
#include <loom/lang/ir.hpp>
#include <loom/lang/lexer.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace loom {

/// A location where a design unit is defined
struct SymbolOrigin {
    std::string file;
    DesignUnitKind kind;
    int line = 0;
};

/// A collision: multiple definitions of the same design unit name
struct SymbolCollision {
    std::string name;
    std::vector<SymbolOrigin> origins;
};

/// Per-file symbol mapping: original name -> mangled name
using SymbolMap = std::unordered_map<std::string, std::string>;

/// Per-file remap table: file_path -> SymbolMap for that file
using FileRemapTable = std::unordered_map<std::string, SymbolMap>;

class SymbolRemapper {
public:
    /// Detect name collisions across all parsed files.
    /// Returns a list of collisions (same design unit name defined in multiple files).
    /// Only considers top-level (depth == 0) design units.
    static std::vector<SymbolCollision> detect_collisions(
        const std::unordered_map<std::string, ParseResult>& parse_results);

    /// Generate a mangled name: name + "_" + sha256(qualifier + ":" + name)[0:8].
    /// The qualifier is typically the file path to distinguish same-name units.
    static std::string mangle(const std::string& name, const std::string& qualifier);

    /// Build per-file remap tables from detected collisions.
    /// Each file that defines a colliding name gets a mapping entry.
    /// Files that instantiate a colliding name also get entries based on
    /// which definition they resolve to (via parse-order proximity).
    static FileRemapTable build_remap_table(
        const std::vector<SymbolCollision>& collisions);

    /// Apply symbol remapping to a token stream (in-place).
    /// Replaces Identifier tokens whose text matches a key in the mapping.
    static void remap_tokens(
        std::vector<VerilogToken>& tokens,
        const SymbolMap& mapping);

    /// Apply remapping to a parse result (modifies unit names and
    /// instantiation references in-place).
    static void remap_parse_result(
        ParseResult& result,
        const SymbolMap& mapping);

    /// Format collision report (human-readable, GCC-style).
    static std::string format_report(const std::vector<SymbolCollision>& collisions);

    /// Format collision report as JSON.
    static std::string format_json(const std::vector<SymbolCollision>& collisions);
};

} // namespace loom
