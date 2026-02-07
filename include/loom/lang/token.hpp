#pragma once

#include <string>
#include <vector>

namespace loom {

// Source position for error reporting
struct SourcePos {
    std::string file;
    int line = 1;
    int col = 1;
};

// Generic token with type parameter
template<typename T>
struct Token {
    T type;
    std::string text;
    SourcePos pos;
};

// Comment types preserved in token stream
enum class CommentKind {
    Line,        // // regular comment
    Block,       // /* block comment */
    DocLine,     // /// doc comment
    Suppression  // // loom: ignore[rule-id]
};

struct Comment {
    CommentKind kind;
    std::string text;     // content without comment markers
    SourcePos pos;
    std::string rule_id;  // for Suppression: the rule id (or "*")
};

} // namespace loom
