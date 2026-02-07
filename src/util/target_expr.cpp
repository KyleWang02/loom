#include <loom/target_expr.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace loom {

// ---------------------------------------------------------------------------
// TargetExpr static constructors
// ---------------------------------------------------------------------------

TargetExpr TargetExpr::wildcard() {
    TargetExpr e;
    e.kind_ = Wildcard;
    return e;
}

TargetExpr TargetExpr::identifier(std::string name) {
    TargetExpr e;
    e.kind_ = Identifier;
    e.name_ = std::move(name);
    return e;
}

TargetExpr TargetExpr::all(std::vector<TargetExpr> children) {
    TargetExpr e;
    e.kind_ = All;
    e.children_ = std::move(children);
    return e;
}

TargetExpr TargetExpr::any(std::vector<TargetExpr> children) {
    TargetExpr e;
    e.kind_ = Any;
    e.children_ = std::move(children);
    return e;
}

TargetExpr TargetExpr::negation(TargetExpr child) {
    TargetExpr e;
    e.kind_ = Not;
    e.children_.push_back(std::move(child));
    return e;
}

TargetExpr::Kind TargetExpr::kind() const {
    return kind_;
}

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------

bool TargetExpr::evaluate(const TargetSet& active) const {
    switch (kind_) {
    case Wildcard:
        return true;
    case Identifier:
        return active.count(name_) > 0;
    case All:
        return std::all_of(children_.begin(), children_.end(),
            [&](const TargetExpr& c) { return c.evaluate(active); });
    case Any:
        return std::any_of(children_.begin(), children_.end(),
            [&](const TargetExpr& c) { return c.evaluate(active); });
    case Not:
        return !children_[0].evaluate(active);
    }
    return false; // unreachable
}

// ---------------------------------------------------------------------------
// to_string
// ---------------------------------------------------------------------------

std::string TargetExpr::to_string() const {
    switch (kind_) {
    case Wildcard:
        return "*";
    case Identifier:
        return name_;
    case All: {
        std::string s = "all(";
        for (size_t i = 0; i < children_.size(); ++i) {
            if (i > 0) s += ", ";
            s += children_[i].to_string();
        }
        s += ")";
        return s;
    }
    case Any: {
        std::string s = "any(";
        for (size_t i = 0; i < children_.size(); ++i) {
            if (i > 0) s += ", ";
            s += children_[i].to_string();
        }
        s += ")";
        return s;
    }
    case Not:
        return "not(" + children_[0].to_string() + ")";
    }
    return ""; // unreachable
}

// ---------------------------------------------------------------------------
// Recursive descent parser
// ---------------------------------------------------------------------------

namespace {

struct Parser {
    const std::string& input;
    size_t pos;

    Parser(const std::string& s) : input(s), pos(0) {}

    void skip_ws() {
        while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t')) {
            ++pos;
        }
    }

    bool at_end() const {
        return pos >= input.size();
    }

    char peek() const {
        return input[pos];
    }

    bool try_consume(const std::string& keyword) {
        size_t saved = pos;
        for (size_t i = 0; i < keyword.size(); ++i) {
            if (pos + i >= input.size() || input[pos + i] != keyword[i]) {
                return false;
            }
        }
        pos += keyword.size();
        return true;
    }

    Result<TargetExpr> parse_expr() {
        skip_ws();
        if (at_end()) {
            return LoomError{LoomError::Parse, "unexpected end of input"};
        }

        if (peek() == '*') {
            ++pos;
            return Result<TargetExpr>::ok(TargetExpr::wildcard());
        }

        // Check for keywords: all(, any(, not(
        if (try_consume("all(")) {
            return parse_compound(TargetExpr::Kind::All);
        }
        if (try_consume("any(")) {
            return parse_compound(TargetExpr::Kind::Any);
        }
        if (try_consume("not(")) {
            return parse_not();
        }

        return parse_identifier();
    }

    Result<TargetExpr> parse_compound(TargetExpr::Kind kind) {
        // Already consumed "all(" or "any("
        std::vector<TargetExpr> children;
        skip_ws();

        // Handle empty: all() or any()
        if (!at_end() && peek() == ')') {
            ++pos;
            if (kind == TargetExpr::Kind::All) {
                return Result<TargetExpr>::ok(TargetExpr::all(std::move(children)));
            }
            return Result<TargetExpr>::ok(TargetExpr::any(std::move(children)));
        }

        // Parse first child
        auto first = parse_expr();
        if (first.is_err()) return first;
        children.push_back(std::move(first).value());

        // Parse remaining comma-separated children
        skip_ws();
        while (!at_end() && peek() == ',') {
            ++pos; // consume ','
            auto child = parse_expr();
            if (child.is_err()) return child;
            children.push_back(std::move(child).value());
            skip_ws();
        }

        skip_ws();
        if (at_end() || peek() != ')') {
            return LoomError{LoomError::Parse,
                "expected ')' in target expression",
                "check for unclosed parentheses"};
        }
        ++pos; // consume ')'

        if (kind == TargetExpr::Kind::All) {
            return Result<TargetExpr>::ok(TargetExpr::all(std::move(children)));
        }
        return Result<TargetExpr>::ok(TargetExpr::any(std::move(children)));
    }

    Result<TargetExpr> parse_not() {
        // Already consumed "not("
        auto child = parse_expr();
        if (child.is_err()) return child;

        skip_ws();
        if (at_end() || peek() != ')') {
            return LoomError{LoomError::Parse,
                "expected ')' after not() argument",
                "not() takes exactly one argument"};
        }
        ++pos; // consume ')'

        return Result<TargetExpr>::ok(TargetExpr::negation(std::move(child).value()));
    }

    Result<TargetExpr> parse_identifier() {
        skip_ws();
        if (at_end()) {
            return LoomError{LoomError::Parse, "expected target name"};
        }

        size_t start = pos;
        // First char must be alpha
        if (!std::isalpha(static_cast<unsigned char>(input[pos]))) {
            std::string msg = "invalid target name starting with '";
            msg += input[pos];
            msg += "'";
            return LoomError{LoomError::Parse, msg,
                "target names must start with a letter"};
        }

        ++pos;
        while (pos < input.size() &&
               (std::isalnum(static_cast<unsigned char>(input[pos])) ||
                input[pos] == '_' || input[pos] == '-')) {
            ++pos;
        }

        std::string name = input.substr(start, pos - start);
        return Result<TargetExpr>::ok(TargetExpr::identifier(std::move(name)));
    }
};

} // anonymous namespace

Result<TargetExpr> TargetExpr::parse(const std::string& input) {
    if (input.empty()) {
        return LoomError{LoomError::InvalidArg, "empty target expression"};
    }

    Parser parser(input);
    auto result = parser.parse_expr();
    if (result.is_err()) return result;

    // Check for trailing garbage
    parser.skip_ws();
    if (!parser.at_end()) {
        return LoomError{LoomError::Parse,
            "unexpected characters after target expression",
            "at position " + std::to_string(parser.pos)};
    }

    return result;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

bool is_valid_target_name(const std::string& name) {
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0]))) return false;
    for (size_t i = 1; i < name.size(); ++i) {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

Result<TargetSet> parse_target_set(const std::string& input) {
    if (input.empty()) {
        return LoomError{LoomError::InvalidArg, "empty target set string"};
    }

    TargetSet result;
    std::istringstream stream(input);
    std::string token;

    while (std::getline(stream, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start == std::string::npos) {
            return LoomError{LoomError::Parse,
                "empty target name in target set",
                "check for consecutive commas or trailing commas"};
        }
        std::string name = token.substr(start, end - start + 1);

        if (!is_valid_target_name(name)) {
            return LoomError{LoomError::Parse,
                "invalid target name '" + name + "'",
                "target names must match [a-zA-Z][a-zA-Z0-9_-]*"};
        }

        result.insert(std::move(name));
    }

    return Result<TargetSet>::ok(std::move(result));
}

std::vector<SourceGroup> filter_source_groups(
    const std::vector<SourceGroup>& groups,
    const TargetSet& active)
{
    std::vector<SourceGroup> out;
    for (const auto& g : groups) {
        if (!g.target.has_value() || g.target->evaluate(active)) {
            out.push_back(g);
        }
    }
    return out;
}

} // namespace loom
