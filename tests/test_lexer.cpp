#include <catch2/catch.hpp>
#include <loom/lang/lexer.hpp>
#include <fstream>
#include <sstream>

using namespace loom;

static std::string fixture_dir() {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures";
    return "../tests/fixtures";
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ===== Basic tokenization =====

TEST_CASE("lex empty string", "[lexer]") {
    auto r = lex("");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens.size() == 1); // just Eof
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Eof);
}

TEST_CASE("lex single identifier", "[lexer]") {
    auto r = lex("foo");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens.size() == 2); // foo + Eof
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Identifier);
    REQUIRE(r.value().tokens[0].text == "foo");
}

TEST_CASE("lex keywords", "[lexer]") {
    auto r = lex("module endmodule input output wire reg");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::KwModule);
    REQUIRE(toks[1].type == VerilogTokenType::KwEndmodule);
    REQUIRE(toks[2].type == VerilogTokenType::KwInput);
    REQUIRE(toks[3].type == VerilogTokenType::KwOutput);
    REQUIRE(toks[4].type == VerilogTokenType::KwWire);
    REQUIRE(toks[5].type == VerilogTokenType::KwReg);
}

TEST_CASE("lex SV keywords in SV mode", "[lexer]") {
    auto r = lex("logic interface package always_comb", "<input>", true);
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::KwLogic);
    REQUIRE(toks[1].type == VerilogTokenType::KwInterface);
    REQUIRE(toks[2].type == VerilogTokenType::KwPackage);
    REQUIRE(toks[3].type == VerilogTokenType::KwAlwaysComb);
}

TEST_CASE("SV keywords as identifiers in Verilog mode", "[lexer]") {
    auto r = lex("logic interface", "<input>", false);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Identifier);
    REQUIRE(r.value().tokens[1].type == VerilogTokenType::Identifier);
}

// ===== Numbers =====

TEST_CASE("lex decimal number", "[lexer]") {
    auto r = lex("42");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Number);
    REQUIRE(r.value().tokens[0].text == "42");
}

TEST_CASE("lex hex number", "[lexer]") {
    auto r = lex("8'hFF");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Number);
    REQUIRE(r.value().tokens[0].text == "8'hFF");
}

TEST_CASE("lex binary number", "[lexer]") {
    auto r = lex("4'b1010");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Number);
    REQUIRE(r.value().tokens[0].text == "4'b1010");
}

TEST_CASE("lex unsized base number", "[lexer]") {
    auto r = lex("'h0");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Number);
}

// ===== Strings =====

TEST_CASE("lex string literal", "[lexer]") {
    auto r = lex("\"hello world\"");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::StringLiteral);
    REQUIRE(r.value().tokens[0].text == "\"hello world\"");
}

TEST_CASE("lex string with escape", "[lexer]") {
    auto r = lex("\"line\\n\"");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::StringLiteral);
}

// ===== Comments =====

TEST_CASE("lex line comment", "[lexer]") {
    auto r = lex("foo // this is a comment\nbar");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Identifier);
    REQUIRE(r.value().tokens[0].text == "foo");
    REQUIRE(r.value().tokens[1].type == VerilogTokenType::Identifier);
    REQUIRE(r.value().tokens[1].text == "bar");
    REQUIRE(r.value().comments.size() == 1);
    REQUIRE(r.value().comments[0].kind == CommentKind::Line);
}

TEST_CASE("lex block comment", "[lexer]") {
    auto r = lex("foo /* block */ bar");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens.size() == 3); // foo bar Eof
    REQUIRE(r.value().comments.size() == 1);
    REQUIRE(r.value().comments[0].kind == CommentKind::Block);
    REQUIRE(r.value().comments[0].text == " block ");
}

TEST_CASE("lex doc comment", "[lexer]") {
    auto r = lex("/// This is documentation\nmodule foo;");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().comments.size() == 1);
    REQUIRE(r.value().comments[0].kind == CommentKind::DocLine);
    REQUIRE(r.value().comments[0].text == "This is documentation");
}

TEST_CASE("lex suppression comment", "[lexer]") {
    auto r = lex("// loom: ignore[blocking-in-ff]\nassign x = 1;");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().comments.size() == 1);
    REQUIRE(r.value().comments[0].kind == CommentKind::Suppression);
    REQUIRE(r.value().comments[0].rule_id == "blocking-in-ff");
}

TEST_CASE("lex wildcard suppression", "[lexer]") {
    auto r = lex("// loom: ignore[*]");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().comments[0].kind == CommentKind::Suppression);
    REQUIRE(r.value().comments[0].rule_id == "*");
}

// ===== Operators =====

TEST_CASE("lex assignment operators", "[lexer]") {
    auto r = lex("= <= == != === !==");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::Assign);
    REQUIRE(toks[1].type == VerilogTokenType::NonBlocking);
    REQUIRE(toks[2].type == VerilogTokenType::DoubleEq);
    REQUIRE(toks[3].type == VerilogTokenType::NotEq);
    REQUIRE(toks[4].type == VerilogTokenType::TripleEq);
    REQUIRE(toks[5].type == VerilogTokenType::TripleNotEq);
}

TEST_CASE("lex shift and comparison", "[lexer]") {
    auto r = lex("<< >> < > >= =>");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::LShift);
    REQUIRE(toks[1].type == VerilogTokenType::RShift);
    REQUIRE(toks[2].type == VerilogTokenType::Less);
    REQUIRE(toks[3].type == VerilogTokenType::Greater);
    REQUIRE(toks[4].type == VerilogTokenType::GreaterEq);
    REQUIRE(toks[5].type == VerilogTokenType::FatArrow);
}

TEST_CASE("lex logical operators", "[lexer]") {
    auto r = lex("&& || ** -> ::");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::LogAnd);
    REQUIRE(toks[1].type == VerilogTokenType::LogOr);
    REQUIRE(toks[2].type == VerilogTokenType::Power);
    REQUIRE(toks[3].type == VerilogTokenType::Arrow);
    REQUIRE(toks[4].type == VerilogTokenType::DoubleColon);
}

TEST_CASE("lex punctuation", "[lexer]") {
    auto r = lex("( ) [ ] { } ; , . # @");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].type == VerilogTokenType::LParen);
    REQUIRE(toks[1].type == VerilogTokenType::RParen);
    REQUIRE(toks[2].type == VerilogTokenType::LBracket);
    REQUIRE(toks[3].type == VerilogTokenType::RBracket);
    REQUIRE(toks[4].type == VerilogTokenType::LBrace);
    REQUIRE(toks[5].type == VerilogTokenType::RBrace);
    REQUIRE(toks[6].type == VerilogTokenType::Semicolon);
    REQUIRE(toks[7].type == VerilogTokenType::Comma);
    REQUIRE(toks[8].type == VerilogTokenType::Dot);
    REQUIRE(toks[9].type == VerilogTokenType::Hash);
    REQUIRE(toks[10].type == VerilogTokenType::At);
}

// ===== Directives =====

TEST_CASE("lex preprocessor directive", "[lexer]") {
    auto r = lex("`define WIDTH 8");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Directive);
    REQUIRE(r.value().tokens[0].text == "`define");
}

TEST_CASE("lex include directive", "[lexer]") {
    auto r = lex("`include");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Directive);
    REQUIRE(r.value().tokens[0].text == "`include");
}

// ===== Escaped identifiers =====

TEST_CASE("lex escaped identifier", "[lexer]") {
    auto r = lex("\\bus[0] ");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::EscapedIdentifier);
    REQUIRE(r.value().tokens[0].text == "\\bus[0]");
}

// ===== Position tracking =====

TEST_CASE("position tracking across lines", "[lexer]") {
    auto r = lex("foo\nbar\n  baz", "test.v");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;
    REQUIRE(toks[0].pos.line == 1);
    REQUIRE(toks[0].pos.col == 1);
    REQUIRE(toks[0].pos.file == "test.v");
    REQUIRE(toks[1].pos.line == 2);
    REQUIRE(toks[1].pos.col == 1);
    REQUIRE(toks[2].pos.line == 3);
    REQUIRE(toks[2].pos.col == 3);
}

// ===== Fixture files =====

TEST_CASE("lex counter.v fixture", "[lexer]") {
    auto src = read_file(fixture_dir() + "/counter.v");
    auto r = lex(src, "counter.v");
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;

    // Should find module keyword
    bool found_module = false;
    for (auto& t : toks) {
        if (t.type == VerilogTokenType::KwModule) {
            found_module = true;
            break;
        }
    }
    REQUIRE(found_module);

    // Should find doc comment and suppression
    REQUIRE(r.value().comments.size() >= 2);
    bool has_doc = false;
    bool has_suppression = false;
    for (auto& c : r.value().comments) {
        if (c.kind == CommentKind::DocLine) has_doc = true;
        if (c.kind == CommentKind::Suppression) has_suppression = true;
    }
    REQUIRE(has_doc);
    REQUIRE(has_suppression);
}

TEST_CASE("lex package_example.sv fixture", "[lexer]") {
    auto src = read_file(fixture_dir() + "/package_example.sv");
    auto r = lex(src, "package_example.sv", true);
    REQUIRE(r.is_ok());
    auto& toks = r.value().tokens;

    // Should find SV keywords
    bool found_package = false;
    bool found_interface = false;
    bool found_always_comb = false;
    bool found_always_ff = false;
    for (auto& t : toks) {
        if (t.type == VerilogTokenType::KwPackage) found_package = true;
        if (t.type == VerilogTokenType::KwInterface) found_interface = true;
        if (t.type == VerilogTokenType::KwAlwaysComb) found_always_comb = true;
        if (t.type == VerilogTokenType::KwAlwaysFf) found_always_ff = true;
    }
    REQUIRE(found_package);
    REQUIRE(found_interface);
    REQUIRE(found_always_comb);
    REQUIRE(found_always_ff);
}

TEST_CASE("lex simple_module.v fixture", "[lexer]") {
    auto src = read_file(fixture_dir() + "/simple_module.v");
    auto r = lex(src, "simple_module.v");
    REQUIRE(r.is_ok());
    // Should have tokens and no errors
    REQUIRE(r.value().tokens.size() > 10);
    REQUIRE(r.value().tokens.back().type == VerilogTokenType::Eof);
}

// ===== Edge cases =====

TEST_CASE("lex number with x and z", "[lexer]") {
    auto r = lex("4'bxxzz");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Number);
    REQUIRE(r.value().tokens[0].text == "4'bxxzz");
}

TEST_CASE("lex system function", "[lexer]") {
    auto r = lex("$display");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().tokens[0].type == VerilogTokenType::Identifier);
    REQUIRE(r.value().tokens[0].text == "$display");
}

TEST_CASE("token name function", "[lexer]") {
    REQUIRE(std::string(verilog_token_name(VerilogTokenType::Identifier)) == "Identifier");
    REQUIRE(std::string(verilog_token_name(VerilogTokenType::Eof)) == "Eof");
    REQUIRE(std::string(verilog_token_name(VerilogTokenType::KwModule)) == "Keyword");
}
