#include <loom/swap.hpp>
#include <sstream>
#include <algorithm>

namespace loom {

// Trim leading and trailing whitespace from a string view
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// Build a hint listing available variable names
static std::string available_vars_hint(const SwapMap& vars) {
    if (vars.empty()) return "no variables defined";
    std::string hint = "available variables: ";
    bool first = true;
    // Sort keys for deterministic output
    std::vector<std::string> keys;
    keys.reserve(vars.size());
    for (const auto& kv : vars) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        if (!first) hint += ", ";
        hint += k;
        first = false;
    }
    return hint;
}

Result<std::string> swap_template(const std::string& tmpl, const SwapMap& vars) {
    std::string out;
    out.reserve(tmpl.size());
    size_t i = 0;

    while (i < tmpl.size()) {
        // Check for escaped opening: \{{
        if (i + 2 < tmpl.size() && tmpl[i] == '\\' && tmpl[i + 1] == '{' && tmpl[i + 2] == '{') {
            out += "{{";
            i += 3;
            continue;
        }

        // Check for opening {{
        if (i + 1 < tmpl.size() && tmpl[i] == '{' && tmpl[i + 1] == '{') {
            size_t start = i + 2;
            size_t end = tmpl.find("}}", start);
            if (end == std::string::npos) {
                return LoomError(LoomError::Parse,
                    "unclosed '{{' in template at position " + std::to_string(i));
            }

            std::string varname = trim(tmpl.substr(start, end - start));
            if (varname.empty()) {
                return LoomError(LoomError::Parse,
                    "empty variable name in template at position " + std::to_string(i));
            }

            auto it = vars.find(varname);
            if (it == vars.end()) {
                return LoomError(LoomError::NotFound,
                    "undefined variable '" + varname + "' in template",
                    available_vars_hint(vars));
            }

            out += it->second;
            i = end + 2;
            continue;
        }

        out.push_back(tmpl[i]);
        i++;
    }

    return Result<std::string>::ok(std::move(out));
}

std::string swap_template_lenient(const std::string& tmpl, const SwapMap& vars) {
    std::string out;
    out.reserve(tmpl.size());
    size_t i = 0;

    while (i < tmpl.size()) {
        // Check for escaped opening: \{{
        if (i + 2 < tmpl.size() && tmpl[i] == '\\' && tmpl[i + 1] == '{' && tmpl[i + 2] == '{') {
            out += "{{";
            i += 3;
            continue;
        }

        // Check for opening {{
        if (i + 1 < tmpl.size() && tmpl[i] == '{' && tmpl[i + 1] == '{') {
            size_t start = i + 2;
            size_t end = tmpl.find("}}", start);
            if (end == std::string::npos) {
                // Lenient: leave as-is
                out += "{{";
                i += 2;
                continue;
            }

            std::string varname = trim(tmpl.substr(start, end - start));
            if (varname.empty()) {
                // Leave empty placeholder as-is
                out += tmpl.substr(i, end + 2 - i);
                i = end + 2;
                continue;
            }

            auto it = vars.find(varname);
            if (it == vars.end()) {
                // Leave undefined variable as-is
                out += tmpl.substr(i, end + 2 - i);
                i = end + 2;
                continue;
            }

            out += it->second;
            i = end + 2;
            continue;
        }

        out.push_back(tmpl[i]);
        i++;
    }

    return out;
}

} // namespace loom
