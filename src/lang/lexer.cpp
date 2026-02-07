#include <loom/lang/lexer.hpp>
#include <cctype>
#include <algorithm>

namespace loom {

// ---------------------------------------------------------------------------
// Keyword tables
// ---------------------------------------------------------------------------

const std::unordered_map<std::string, VerilogTokenType>& verilog_keywords() {
    static const std::unordered_map<std::string, VerilogTokenType> table = {
        // Verilog-2001 keywords
        {"module",       VerilogTokenType::KwModule},
        {"endmodule",    VerilogTokenType::KwEndmodule},
        {"input",        VerilogTokenType::KwInput},
        {"output",       VerilogTokenType::KwOutput},
        {"inout",        VerilogTokenType::KwInout},
        {"wire",         VerilogTokenType::KwWire},
        {"reg",          VerilogTokenType::KwReg},
        {"parameter",    VerilogTokenType::KwParameter},
        {"localparam",   VerilogTokenType::KwLocalparam},
        {"assign",       VerilogTokenType::KwAssign},
        {"always",       VerilogTokenType::KwAlways},
        {"initial",      VerilogTokenType::KwInitial},
        {"begin",        VerilogTokenType::KwBegin},
        {"end",          VerilogTokenType::KwEnd},
        {"if",           VerilogTokenType::KwIf},
        {"else",         VerilogTokenType::KwElse},
        {"case",         VerilogTokenType::KwCase},
        {"casex",        VerilogTokenType::KwCasex},
        {"casez",        VerilogTokenType::KwCasez},
        {"endcase",      VerilogTokenType::KwEndcase},
        {"for",          VerilogTokenType::KwFor},
        {"while",        VerilogTokenType::KwWhile},
        {"generate",     VerilogTokenType::KwGenerate},
        {"endgenerate",  VerilogTokenType::KwEndgenerate},
        {"function",     VerilogTokenType::KwFunction},
        {"endfunction",  VerilogTokenType::KwEndfunction},
        {"task",         VerilogTokenType::KwTask},
        {"endtask",      VerilogTokenType::KwEndtask},
        {"defparam",     VerilogTokenType::KwDefparam},
        {"default",      VerilogTokenType::KwDefault},
        {"posedge",      VerilogTokenType::KwPosedge},
        {"negedge",      VerilogTokenType::KwNegedge},
        {"or",           VerilogTokenType::KwOr},
        {"and",          VerilogTokenType::KwAnd},
        {"not",          VerilogTokenType::KwNot},
        {"supply0",      VerilogTokenType::KwSupply0},
        {"supply1",      VerilogTokenType::KwSupply1},
        {"integer",      VerilogTokenType::KwInteger},
        {"real",         VerilogTokenType::KwReal},
        {"time",         VerilogTokenType::KwTime},
        {"genvar",       VerilogTokenType::KwGenvar},
        // SystemVerilog keywords
        {"logic",        VerilogTokenType::KwLogic},
        {"bit",          VerilogTokenType::KwBit},
        {"byte",         VerilogTokenType::KwByte},
        {"shortint",     VerilogTokenType::KwShortint},
        {"int",          VerilogTokenType::KwInt},
        {"longint",      VerilogTokenType::KwLongint},
        {"interface",    VerilogTokenType::KwInterface},
        {"endinterface", VerilogTokenType::KwEndinterface},
        {"package",      VerilogTokenType::KwPackage},
        {"endpackage",   VerilogTokenType::KwEndpackage},
        {"class",        VerilogTokenType::KwClass},
        {"endclass",     VerilogTokenType::KwEndclass},
        {"import",       VerilogTokenType::KwImport},
        {"export",       VerilogTokenType::KwExport},
        {"typedef",      VerilogTokenType::KwTypedef},
        {"enum",         VerilogTokenType::KwEnum},
        {"struct",       VerilogTokenType::KwStruct},
        {"union",        VerilogTokenType::KwUnion},
        {"virtual",      VerilogTokenType::KwVirtual},
        {"extends",      VerilogTokenType::KwExtends},
        {"implements",   VerilogTokenType::KwImplements},
        {"modport",      VerilogTokenType::KwModport},
        {"clocking",     VerilogTokenType::KwClocking},
        {"endclocking",  VerilogTokenType::KwEndclocking},
        {"property",     VerilogTokenType::KwProperty},
        {"endproperty",  VerilogTokenType::KwEndproperty},
        {"sequence",     VerilogTokenType::KwSequence},
        {"endsequence",  VerilogTokenType::KwEndsequence},
        {"assert",       VerilogTokenType::KwAssert},
        {"assume",       VerilogTokenType::KwAssume},
        {"cover",        VerilogTokenType::KwCover},
        {"constraint",   VerilogTokenType::KwConstraint},
        {"rand",         VerilogTokenType::KwRand},
        {"randc",        VerilogTokenType::KwRandc},
        {"unique",       VerilogTokenType::KwUnique},
        {"priority",     VerilogTokenType::KwPriority},
        {"always_comb",  VerilogTokenType::KwAlwaysComb},
        {"always_ff",    VerilogTokenType::KwAlwaysFf},
        {"always_latch", VerilogTokenType::KwAlwaysLatch},
        {"foreach",      VerilogTokenType::KwForeach},
        {"return",       VerilogTokenType::KwReturn},
        {"void",         VerilogTokenType::KwVoid},
        {"automatic",    VerilogTokenType::KwAutomatic},
        {"static",       VerilogTokenType::KwStatic},
        {"const",        VerilogTokenType::KwConst},
        {"ref",          VerilogTokenType::KwRef},
        {"program",      VerilogTokenType::KwProgram},
        {"endprogram",   VerilogTokenType::KwEndprogram},
    };
    return table;
}

const char* verilog_token_name(VerilogTokenType t) {
    switch (t) {
    case VerilogTokenType::Identifier:       return "Identifier";
    case VerilogTokenType::EscapedIdentifier:return "EscapedIdentifier";
    case VerilogTokenType::Number:           return "Number";
    case VerilogTokenType::StringLiteral:    return "StringLiteral";
    case VerilogTokenType::Directive:        return "Directive";
    case VerilogTokenType::LParen:           return "LParen";
    case VerilogTokenType::RParen:           return "RParen";
    case VerilogTokenType::LBracket:         return "LBracket";
    case VerilogTokenType::RBracket:         return "RBracket";
    case VerilogTokenType::LBrace:           return "LBrace";
    case VerilogTokenType::RBrace:           return "RBrace";
    case VerilogTokenType::Semicolon:        return "Semicolon";
    case VerilogTokenType::Colon:            return "Colon";
    case VerilogTokenType::Comma:            return "Comma";
    case VerilogTokenType::Dot:              return "Dot";
    case VerilogTokenType::Hash:             return "Hash";
    case VerilogTokenType::At:               return "At";
    case VerilogTokenType::Assign:           return "Assign";
    case VerilogTokenType::NonBlocking:      return "NonBlocking";
    case VerilogTokenType::Plus:             return "Plus";
    case VerilogTokenType::Minus:            return "Minus";
    case VerilogTokenType::Star:             return "Star";
    case VerilogTokenType::Slash:            return "Slash";
    case VerilogTokenType::Percent:          return "Percent";
    case VerilogTokenType::Ampersand:        return "Ampersand";
    case VerilogTokenType::Pipe:             return "Pipe";
    case VerilogTokenType::Caret:            return "Caret";
    case VerilogTokenType::Tilde:            return "Tilde";
    case VerilogTokenType::Bang:             return "Bang";
    case VerilogTokenType::Question:         return "Question";
    case VerilogTokenType::DoubleColon:      return "DoubleColon";
    case VerilogTokenType::Arrow:            return "Arrow";
    case VerilogTokenType::FatArrow:         return "FatArrow";
    case VerilogTokenType::DoubleEq:         return "DoubleEq";
    case VerilogTokenType::NotEq:            return "NotEq";
    case VerilogTokenType::TripleEq:         return "TripleEq";
    case VerilogTokenType::TripleNotEq:      return "TripleNotEq";
    case VerilogTokenType::LessEq:           return "LessEq";
    case VerilogTokenType::GreaterEq:        return "GreaterEq";
    case VerilogTokenType::Less:             return "Less";
    case VerilogTokenType::Greater:          return "Greater";
    case VerilogTokenType::LShift:           return "LShift";
    case VerilogTokenType::RShift:           return "RShift";
    case VerilogTokenType::LogAnd:           return "LogAnd";
    case VerilogTokenType::LogOr:            return "LogOr";
    case VerilogTokenType::Power:            return "Power";
    case VerilogTokenType::Eof:              return "Eof";
    case VerilogTokenType::Unknown:          return "Unknown";
    default:
        // Keywords — return generic name
        return "Keyword";
    }
}

// ---------------------------------------------------------------------------
// Lexer state machine
// ---------------------------------------------------------------------------

namespace {

struct Lexer {
    const std::string& source;
    const std::string& filename;
    bool is_sv;
    size_t pos;
    int line;
    int col;

    std::vector<VerilogToken> tokens;
    std::vector<Comment> comments;

    Lexer(const std::string& src, const std::string& fname, bool sv)
        : source(src), filename(fname), is_sv(sv), pos(0), line(1), col(1) {}

    bool at_end() const { return pos >= source.size(); }

    char peek() const { return source[pos]; }

    char peek_next() const {
        return (pos + 1 < source.size()) ? source[pos + 1] : '\0';
    }

    char advance() {
        char c = source[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }

    SourcePos current_pos() const {
        return {filename, line, col};
    }

    void emit(VerilogTokenType type, const std::string& text, SourcePos p) {
        tokens.push_back({type, text, p});
    }

    Result<LexResult> run() {
        while (!at_end()) {
            skip_whitespace();
            if (at_end()) break;

            auto p = current_pos();
            char c = peek();

            // Comments
            if (c == '/' && peek_next() == '/') {
                lex_line_comment(p);
                continue;
            }
            if (c == '/' && peek_next() == '*') {
                lex_block_comment(p);
                continue;
            }

            // String literals
            if (c == '"') {
                auto r = lex_string(p);
                if (r.is_err()) return std::move(r).error();
                continue;
            }

            // Preprocessor directives
            if (c == '`') {
                lex_directive(p);
                continue;
            }

            // Escaped identifiers
            if (c == '\\') {
                lex_escaped_identifier(p);
                continue;
            }

            // Numbers (digit or 'N where N is size)
            if (std::isdigit(static_cast<unsigned char>(c))) {
                lex_number(p);
                continue;
            }
            // Sized numbers starting with '
            if (c == '\'' && pos + 1 < source.size() &&
                (peek_next() == 'b' || peek_next() == 'B' ||
                 peek_next() == 'o' || peek_next() == 'O' ||
                 peek_next() == 'h' || peek_next() == 'H' ||
                 peek_next() == 'd' || peek_next() == 'D')) {
                lex_number(p);
                continue;
            }

            // Identifiers and keywords
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
                lex_identifier(p);
                continue;
            }

            // Operators and punctuation
            lex_operator(p);
        }

        emit(VerilogTokenType::Eof, "", current_pos());

        LexResult result;
        result.tokens = std::move(tokens);
        result.comments = std::move(comments);
        return Result<LexResult>::ok(std::move(result));
    }

    void skip_whitespace() {
        while (!at_end()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
            } else {
                break;
            }
        }
    }

    void lex_line_comment(SourcePos p) {
        advance(); // /
        advance(); // /

        // Check for /// doc comment
        bool is_doc = (!at_end() && peek() == '/' && (pos + 1 >= source.size() || peek_next() != '/'));

        std::string text;
        if (is_doc) advance(); // consume third /

        // Check for suppression: // loom: ignore[...]
        size_t start = pos;
        while (!at_end() && peek() != '\n') {
            text += advance();
        }

        // Trim leading space
        size_t first_nonspace = text.find_first_not_of(' ');
        if (first_nonspace != std::string::npos) {
            text = text.substr(first_nonspace);
        }

        if (is_doc) {
            comments.push_back({CommentKind::DocLine, text, p, ""});
        } else if (text.substr(0, 13) == "loom: ignore[") {
            // Parse suppression rule id
            size_t bracket_start = 12; // position of [
            size_t bracket_end = text.find(']', bracket_start + 1);
            if (bracket_end != std::string::npos) {
                std::string rule_id = text.substr(bracket_start + 1,
                    bracket_end - bracket_start - 1);
                comments.push_back({CommentKind::Suppression, text, p, rule_id});
            } else {
                comments.push_back({CommentKind::Line, text, p, ""});
            }
        } else {
            comments.push_back({CommentKind::Line, text, p, ""});
        }
    }

    void lex_block_comment(SourcePos p) {
        advance(); // /
        advance(); // *
        std::string text;
        while (!at_end()) {
            if (peek() == '*' && peek_next() == '/') {
                advance(); // *
                advance(); // /
                comments.push_back({CommentKind::Block, text, p, ""});
                return;
            }
            text += advance();
        }
        // Unclosed block comment — store what we have
        comments.push_back({CommentKind::Block, text, p, ""});
    }

    Status lex_string(SourcePos p) {
        advance(); // opening "
        std::string text = "\"";
        while (!at_end()) {
            char c = peek();
            if (c == '\\') {
                text += advance();
                if (!at_end()) text += advance();
                continue;
            }
            if (c == '"') {
                text += advance();
                emit(VerilogTokenType::StringLiteral, text, p);
                return ok_status();
            }
            if (c == '\n') {
                // Unterminated string at end of line
                emit(VerilogTokenType::StringLiteral, text, p);
                return ok_status();
            }
            text += advance();
        }
        emit(VerilogTokenType::StringLiteral, text, p);
        return ok_status();
    }

    void lex_directive(SourcePos p) {
        std::string text;
        text += advance(); // `
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) ||
                              peek() == '_')) {
            text += advance();
        }
        emit(VerilogTokenType::Directive, text, p);
    }

    void lex_escaped_identifier(SourcePos p) {
        std::string text;
        text += advance(); // backslash
        while (!at_end() && peek() != ' ' && peek() != '\t' &&
               peek() != '\n' && peek() != '\r') {
            text += advance();
        }
        emit(VerilogTokenType::EscapedIdentifier, text, p);
    }

    void lex_number(SourcePos p) {
        std::string text;

        // Consume digits before possible base
        while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) ||
                              peek() == '_')) {
            text += advance();
        }

        // Check for base specifier: 'b, 'o, 'h, 'd
        if (!at_end() && peek() == '\'') {
            text += advance(); // '
            if (!at_end() && (peek() == 'b' || peek() == 'B' ||
                               peek() == 'o' || peek() == 'O' ||
                               peek() == 'h' || peek() == 'H' ||
                               peek() == 'd' || peek() == 'D')) {
                text += advance(); // base letter
                // Consume hex/oct/bin digits
                while (!at_end() && (std::isxdigit(static_cast<unsigned char>(peek())) ||
                                      peek() == '_' || peek() == 'x' || peek() == 'X' ||
                                      peek() == 'z' || peek() == 'Z' || peek() == '?')) {
                    text += advance();
                }
            }
        }
        // Check for decimal point (real number)
        else if (!at_end() && peek() == '.' &&
                 pos + 1 < source.size() &&
                 std::isdigit(static_cast<unsigned char>(source[pos + 1]))) {
            text += advance(); // .
            while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) ||
                                  peek() == '_')) {
                text += advance();
            }
        }

        emit(VerilogTokenType::Number, text, p);
    }

    void lex_identifier(SourcePos p) {
        std::string text;
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) ||
                              peek() == '_' || peek() == '$')) {
            text += advance();
        }

        // Check keywords
        auto& kws = verilog_keywords();
        auto it = kws.find(text);
        if (it != kws.end()) {
            // For SV-only keywords, only emit as keyword in SV mode
            auto type = it->second;
            bool is_sv_keyword = (type >= VerilogTokenType::KwLogic &&
                                  type <= VerilogTokenType::KwEndprogram);
            if (is_sv_keyword && !is_sv) {
                emit(VerilogTokenType::Identifier, text, p);
            } else {
                emit(type, text, p);
            }
        } else {
            emit(VerilogTokenType::Identifier, text, p);
        }
    }

    void lex_operator(SourcePos p) {
        char c = advance();
        switch (c) {
        case '(': emit(VerilogTokenType::LParen, "(", p); break;
        case ')': emit(VerilogTokenType::RParen, ")", p); break;
        case '[': emit(VerilogTokenType::LBracket, "[", p); break;
        case ']': emit(VerilogTokenType::RBracket, "]", p); break;
        case '{': emit(VerilogTokenType::LBrace, "{", p); break;
        case '}': emit(VerilogTokenType::RBrace, "}", p); break;
        case ';': emit(VerilogTokenType::Semicolon, ";", p); break;
        case ',': emit(VerilogTokenType::Comma, ",", p); break;
        case '.': emit(VerilogTokenType::Dot, ".", p); break;
        case '#': emit(VerilogTokenType::Hash, "#", p); break;
        case '@': emit(VerilogTokenType::At, "@", p); break;
        case '~': emit(VerilogTokenType::Tilde, "~", p); break;
        case '?': emit(VerilogTokenType::Question, "?", p); break;
        case '%': emit(VerilogTokenType::Percent, "%", p); break;
        case '+': emit(VerilogTokenType::Plus, "+", p); break;
        case '-':
            if (!at_end() && peek() == '>') {
                advance();
                emit(VerilogTokenType::Arrow, "->", p);
            } else {
                emit(VerilogTokenType::Minus, "-", p);
            }
            break;
        case '*':
            if (!at_end() && peek() == '*') {
                advance();
                emit(VerilogTokenType::Power, "**", p);
            } else {
                emit(VerilogTokenType::Star, "*", p);
            }
            break;
        case '/': emit(VerilogTokenType::Slash, "/", p); break;
        case '&':
            if (!at_end() && peek() == '&') {
                advance();
                emit(VerilogTokenType::LogAnd, "&&", p);
            } else {
                emit(VerilogTokenType::Ampersand, "&", p);
            }
            break;
        case '|':
            if (!at_end() && peek() == '|') {
                advance();
                emit(VerilogTokenType::LogOr, "||", p);
            } else {
                emit(VerilogTokenType::Pipe, "|", p);
            }
            break;
        case '^': emit(VerilogTokenType::Caret, "^", p); break;
        case ':':
            if (!at_end() && peek() == ':') {
                advance();
                emit(VerilogTokenType::DoubleColon, "::", p);
            } else {
                emit(VerilogTokenType::Colon, ":", p);
            }
            break;
        case '=':
            if (!at_end() && peek() == '=') {
                advance();
                if (!at_end() && peek() == '=') {
                    advance();
                    emit(VerilogTokenType::TripleEq, "===", p);
                } else {
                    emit(VerilogTokenType::DoubleEq, "==", p);
                }
            } else if (!at_end() && peek() == '>') {
                advance();
                emit(VerilogTokenType::FatArrow, "=>", p);
            } else {
                emit(VerilogTokenType::Assign, "=", p);
            }
            break;
        case '!':
            if (!at_end() && peek() == '=') {
                advance();
                if (!at_end() && peek() == '=') {
                    advance();
                    emit(VerilogTokenType::TripleNotEq, "!==", p);
                } else {
                    emit(VerilogTokenType::NotEq, "!=", p);
                }
            } else {
                emit(VerilogTokenType::Bang, "!", p);
            }
            break;
        case '<':
            if (!at_end() && peek() == '=') {
                advance();
                emit(VerilogTokenType::NonBlocking, "<=", p);
            } else if (!at_end() && peek() == '<') {
                advance();
                emit(VerilogTokenType::LShift, "<<", p);
            } else {
                emit(VerilogTokenType::Less, "<", p);
            }
            break;
        case '>':
            if (!at_end() && peek() == '=') {
                advance();
                emit(VerilogTokenType::GreaterEq, ">=", p);
            } else if (!at_end() && peek() == '>') {
                advance();
                emit(VerilogTokenType::RShift, ">>", p);
            } else {
                emit(VerilogTokenType::Greater, ">", p);
            }
            break;
        default:
            emit(VerilogTokenType::Unknown, std::string(1, c), p);
            break;
        }
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<LexResult> lex(const std::string& source,
                      const std::string& filename,
                      bool is_sv) {
    Lexer lexer(source, filename, is_sv);
    return lexer.run();
}

} // namespace loom
