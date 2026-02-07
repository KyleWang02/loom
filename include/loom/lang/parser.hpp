#pragma once

#include <loom/lang/ir.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/result.hpp>
#include <string>

namespace loom {

// Parse a lexed Verilog/SystemVerilog file into structural IR.
// Always produces partial results even on errors (diagnostics accumulate).
Result<ParseResult> parse(const LexResult& lex_result,
                          const std::string& filename = "<input>",
                          bool is_sv = false);

} // namespace loom
