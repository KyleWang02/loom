#pragma once

#include <loom/result.hpp>
#include <string>
#include <unordered_map>

namespace loom {

using SwapMap = std::unordered_map<std::string, std::string>;

// Substitute {{ var }} placeholders in a template string.
// Strict mode: returns error on undefined variables or unclosed braces.
// Supports \{{ to produce literal {{ in output.
Result<std::string> swap_template(const std::string& tmpl, const SwapMap& vars);

// Lenient substitution: undefined variables are left as-is ({{ var }}),
// unclosed braces are left as-is. Never returns an error.
std::string swap_template_lenient(const std::string& tmpl, const SwapMap& vars);

} // namespace loom
