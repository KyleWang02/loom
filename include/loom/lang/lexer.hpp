#pragma once

#include <loom/lang/verilog_token.hpp>
#include <loom/result.hpp>
#include <string>
#include <vector>

namespace loom {

struct LexResult {
    std::vector<VerilogToken> tokens;
    std::vector<Comment> comments;
};

// Lex Verilog/SystemVerilog source into tokens + preserved comments.
// The is_sv flag enables SystemVerilog keywords (logic, interface, etc.)
Result<LexResult> lex(const std::string& source,
                      const std::string& filename = "<input>",
                      bool is_sv = false);

} // namespace loom
