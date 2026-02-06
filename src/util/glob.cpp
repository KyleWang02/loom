#include <loom/glob.hpp>
#include <algorithm>

namespace loom {

// ---- Helpers ----

static std::string normalize_path(const std::string& p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        // Collapse consecutive slashes
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(c);
    }
    // Remove trailing slash (unless the entire string is "/")
    if (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

static std::vector<std::string> split_segments(const std::string& s) {
    std::vector<std::string> segs;
    std::string cur;
    for (char c : s) {
        if (c == '/') {
            segs.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    segs.push_back(cur);
    return segs;
}

// Match a single segment against a pattern segment (no '/' in either).
// Supports *, ?, [abc], [a-z], [!...].
static bool match_segment(const std::string& pat, size_t pi,
                          const std::string& str, size_t si) {
    while (pi < pat.size() && si < str.size()) {
        char pc = pat[pi];

        if (pc == '*') {
            // '*' matches zero or more chars (not '/')
            // Try matching remaining pattern from every position
            pi++;
            // Consecutive stars in a single segment collapse
            while (pi < pat.size() && pat[pi] == '*') pi++;
            // If pattern exhausted, star matches rest of segment
            if (pi == pat.size()) return true;
            // Try advancing str position
            for (size_t k = si; k <= str.size(); k++) {
                if (match_segment(pat, pi, str, k)) return true;
            }
            return false;
        }

        if (pc == '?') {
            // Matches any single char
            pi++;
            si++;
            continue;
        }

        if (pc == '[') {
            // Character class
            pi++; // skip '['
            bool negate = false;
            if (pi < pat.size() && pat[pi] == '!') {
                negate = true;
                pi++;
            }
            bool matched = false;
            char sc = str[si];
            // Parse until ']'
            while (pi < pat.size() && pat[pi] != ']') {
                char lo = pat[pi];
                if (pi + 2 < pat.size() && pat[pi + 1] == '-' && pat[pi + 2] != ']') {
                    char hi = pat[pi + 2];
                    if (sc >= lo && sc <= hi) matched = true;
                    pi += 3;
                } else {
                    if (sc == lo) matched = true;
                    pi++;
                }
            }
            if (pi < pat.size()) pi++; // skip ']'
            if (negate) matched = !matched;
            if (!matched) return false;
            si++;
            continue;
        }

        // Literal character
        if (pc != str[si]) return false;
        pi++;
        si++;
    }

    // Consume trailing stars in pattern
    while (pi < pat.size() && pat[pi] == '*') pi++;

    return pi == pat.size() && si == str.size();
}

// Recursive matching over path segments, handling '**'.
static bool match_segments(const std::vector<std::string>& pat_segs, size_t pi,
                           const std::vector<std::string>& path_segs, size_t si) {
    while (pi < pat_segs.size() && si < path_segs.size()) {
        const auto& ps = pat_segs[pi];

        if (ps == "**") {
            // Collapse consecutive '**' segments
            while (pi < pat_segs.size() && pat_segs[pi] == "**") pi++;
            // If pattern exhausted, '**' matches everything remaining
            if (pi == pat_segs.size()) return true;
            // Try matching remaining pattern from every remaining path position
            for (size_t k = si; k <= path_segs.size(); k++) {
                if (match_segments(pat_segs, pi, path_segs, k)) return true;
            }
            return false;
        }

        if (!match_segment(ps, 0, path_segs[si], 0)) return false;
        pi++;
        si++;
    }

    // Consume trailing '**' in pattern
    while (pi < pat_segs.size() && pat_segs[pi] == "**") pi++;

    return pi == pat_segs.size() && si == path_segs.size();
}

// ---- Public API ----

bool glob_match(const std::string& pattern, const std::string& path) {
    auto norm_pat = normalize_path(pattern);
    auto norm_path = normalize_path(path);

    auto pat_segs = split_segments(norm_pat);
    auto path_segs = split_segments(norm_path);

    return match_segments(pat_segs, 0, path_segs, 0);
}

bool glob_is_negation(const std::string& pattern, std::string& inner) {
    if (!pattern.empty() && pattern[0] == '!') {
        inner = pattern.substr(1);
        return true;
    }
    return false;
}

Result<std::vector<std::string>> glob_expand(
    const std::string& pattern,
    const std::filesystem::path& root_dir)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root_dir, ec)) {
        return LoomError(LoomError::IO,
            "glob_expand: root directory does not exist: " + root_dir.string());
    }

    std::vector<std::string> results;
    auto norm_pattern = normalize_path(pattern);

    for (auto& entry : std::filesystem::recursive_directory_iterator(root_dir, ec)) {
        if (ec) {
            return LoomError(LoomError::IO,
                "glob_expand: error iterating directory: " + ec.message());
        }
        if (!entry.is_regular_file()) continue;

        // Get path relative to root_dir
        auto rel = std::filesystem::relative(entry.path(), root_dir, ec);
        if (ec) continue;

        auto rel_str = normalize_path(rel.string());
        if (glob_match(norm_pattern, rel_str)) {
            results.push_back(rel_str);
        }
    }

    std::sort(results.begin(), results.end());
    return Result<std::vector<std::string>>::ok(std::move(results));
}

std::vector<std::string> glob_filter(
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& paths)
{
    std::vector<std::string> result;

    for (const auto& path : paths) {
        bool included = false;
        // Process patterns in order — last matching pattern wins
        for (const auto& pat : patterns) {
            std::string inner;
            if (glob_is_negation(pat, inner)) {
                // Exclude pattern
                if (glob_match(inner, path)) {
                    included = false;
                }
            } else {
                // Include pattern
                if (glob_match(pat, path)) {
                    included = true;
                }
            }
        }
        if (included) {
            result.push_back(path);
        }
    }

    return result;
}

} // namespace loom
