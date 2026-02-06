#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace loom {

// Match a glob pattern against a path (both normalized to forward slashes).
// Supports: * (any chars except /), ? (single char except /),
//           ** (zero or more path segments), [abc], [a-z], [!0-9]
bool glob_match(const std::string& pattern, const std::string& path);

// Check if pattern is a negation pattern (prefixed with '!').
// If so, stores the inner pattern (without '!') in `inner` and returns true.
bool glob_is_negation(const std::string& pattern, std::string& inner);

// Expand a glob pattern against the filesystem rooted at root_dir.
// Returns matching paths relative to root_dir.
Result<std::vector<std::string>> glob_expand(
    const std::string& pattern,
    const std::filesystem::path& root_dir);

// Apply ordered include/exclude patterns to a list of paths.
// Patterns prefixed with '!' exclude; others include.
// Returns paths that match at least one include and no subsequent exclude.
std::vector<std::string> glob_filter(
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& paths);

} // namespace loom
