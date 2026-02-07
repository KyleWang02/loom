#include <loom/lang/parser.hpp>
#include <algorithm>

namespace loom {

namespace {

using TT = VerilogTokenType;

// ---------------------------------------------------------------------------
// Parser state machine
// ---------------------------------------------------------------------------

struct Parser {
    const std::vector<VerilogToken>& tokens;
    const std::string& filename;
    bool is_sv;
    size_t pos;

    ParseResult result;
    int depth;  // nesting depth for design units

    Parser(const std::vector<VerilogToken>& toks,
           const std::string& fname, bool sv)
        : tokens(toks), filename(fname), is_sv(sv), pos(0), depth(0) {}

    // -- Navigation ---------------------------------------------------------

    bool at_end() const {
        return pos >= tokens.size() || tokens[pos].type == TT::Eof;
    }

    const VerilogToken& peek() const {
        return tokens[pos];
    }

    const VerilogToken& peek_at(size_t offset) const {
        size_t idx = pos + offset;
        if (idx >= tokens.size()) return tokens.back(); // Eof
        return tokens[idx];
    }

    const VerilogToken& advance() {
        const auto& tok = tokens[pos];
        if (pos < tokens.size() && tok.type != TT::Eof) ++pos;
        return tok;
    }

    bool check(TT type) const {
        return !at_end() && peek().type == type;
    }

    bool match(TT type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    bool expect(TT type) {
        if (match(type)) return true;
        diag("expected " + std::string(verilog_token_name(type)) +
             ", got " + std::string(verilog_token_name(peek().type)));
        return false;
    }

    // -- Diagnostics --------------------------------------------------------

    void diag(const std::string& msg) {
        auto& p = at_end() ? tokens.back().pos : peek().pos;
        result.diagnostics.push_back({msg, p.file, p.line, p.col});
    }

    // -- Skip helpers -------------------------------------------------------

    void skip_to_semicolon() {
        int paren = 0, bracket = 0, brace = 0;
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::LParen) ++paren;
            else if (t == TT::RParen) --paren;
            else if (t == TT::LBracket) ++bracket;
            else if (t == TT::RBracket) --bracket;
            else if (t == TT::LBrace) ++brace;
            else if (t == TT::RBrace) --brace;
            else if (t == TT::Semicolon && paren <= 0 && bracket <= 0 && brace <= 0) {
                advance(); // consume the semicolon
                return;
            }
            advance();
        }
    }

    void skip_balanced(TT open, TT close) {
        if (!match(open)) return;
        int depth = 1;
        while (!at_end() && depth > 0) {
            auto t = peek().type;
            if (t == open) ++depth;
            else if (t == close) --depth;
            advance();
        }
    }

    // Skip to the matching end keyword for a design unit
    // Returns the line of the end keyword
    int skip_to_end_keyword(TT end_kw) {
        int nesting = 1;
        TT begin_kw = corresponding_begin(end_kw);
        while (!at_end()) {
            auto t = peek().type;
            if (t == begin_kw) {
                ++nesting;
            } else if (t == end_kw) {
                --nesting;
                if (nesting == 0) {
                    int end_line = peek().pos.line;
                    advance(); // consume end keyword
                    // Consume optional : name after endmodule
                    if (check(TT::Colon)) {
                        advance();
                        if (check(TT::Identifier)) advance();
                    }
                    // Consume optional semicolon
                    match(TT::Semicolon);
                    return end_line;
                }
            }
            advance();
        }
        return at_end() ? tokens.back().pos.line : peek().pos.line;
    }

    static TT corresponding_begin(TT end_kw) {
        switch (end_kw) {
        case TT::KwEndmodule:    return TT::KwModule;
        case TT::KwEndpackage:   return TT::KwPackage;
        case TT::KwEndinterface: return TT::KwInterface;
        case TT::KwEndclass:     return TT::KwClass;
        case TT::KwEndprogram:   return TT::KwProgram;
        default:                 return TT::Eof; // should not happen
        }
    }

    // -- Top-level dispatch -------------------------------------------------

    void parse_file() {
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::KwModule)    { parse_design_unit(DesignUnitKind::Module, TT::KwEndmodule); }
            else if (t == TT::KwPackage)   { parse_design_unit(DesignUnitKind::Package, TT::KwEndpackage); }
            else if (t == TT::KwInterface) { parse_design_unit(DesignUnitKind::Interface, TT::KwEndinterface); }
            else if (t == TT::KwClass)     { parse_design_unit(DesignUnitKind::Class, TT::KwEndclass); }
            else if (t == TT::KwProgram)   { parse_design_unit(DesignUnitKind::Program, TT::KwEndprogram); }
            else {
                advance(); // skip unrecognized tokens
            }
        }
    }

    // -- Design unit parsing ------------------------------------------------

    void parse_design_unit(DesignUnitKind kind, TT end_kw) {
        DesignUnit unit;
        unit.kind = kind;
        unit.pos = peek().pos;
        unit.start_line = peek().pos.line;
        unit.depth = depth;

        advance(); // consume module/package/interface/class/program keyword

        // Optional: automatic/static lifetime qualifier
        if (check(TT::KwAutomatic) || check(TT::KwStatic)) {
            advance();
        }

        // Extract name
        if (check(TT::Identifier)) {
            unit.name = peek().text;
            advance();
        } else {
            diag("expected design unit name");
            // Error recovery: skip to next structural keyword
            skip_to_next_structural();
            return;
        }

        // For classes: handle extends/implements
        if (kind == DesignUnitKind::Class) {
            if (check(TT::KwExtends)) {
                advance();
                if (check(TT::Identifier)) advance(); // parent class
            }
            if (check(TT::KwImplements)) {
                advance();
                if (check(TT::Identifier)) advance();
            }
        }

        // Parse parameter list #(...)
        if (check(TT::Hash)) {
            parse_parameter_list(unit);
        }

        // Parse port list (...)
        if (check(TT::LParen)) {
            parse_port_list(unit);
        }

        // Consume semicolon after header
        match(TT::Semicolon);

        // For imports before port list (SV: module foo import pkg::*; (...))
        // already handled by the main body loop

        // Parse body
        ++depth;
        parse_body(unit, end_kw);
        --depth;

        result.units.push_back(std::move(unit));
    }

    // -- Body parsing -------------------------------------------------------

    void parse_body(DesignUnit& unit, TT end_kw) {
        while (!at_end()) {
            auto t = peek().type;

            // Check for end of this design unit
            if (t == end_kw) {
                unit.end_line = peek().pos.line;
                advance();
                // Consume optional : name
                if (check(TT::Colon)) {
                    advance();
                    if (check(TT::Identifier)) advance();
                }
                match(TT::Semicolon);
                return;
            }

            // Nested design units
            if (t == TT::KwModule) {
                parse_design_unit(DesignUnitKind::Module, TT::KwEndmodule);
                continue;
            }

            // Non-ANSI port declarations in body
            if (t == TT::KwInput || t == TT::KwOutput || t == TT::KwInout) {
                parse_non_ansi_port(unit);
                continue;
            }

            // Parameters in body
            if (t == TT::KwParameter || t == TT::KwLocalparam) {
                parse_body_parameter(unit);
                continue;
            }

            // Imports
            if (t == TT::KwImport) {
                parse_import(unit);
                continue;
            }

            // Instantiation heuristic: IDENT IDENT ( or IDENT #( IDENT (
            if (t == TT::Identifier) {
                if (try_parse_instantiation(unit)) continue;
            }

            // Always blocks
            if (t == TT::KwAlways || t == TT::KwAlwaysComb ||
                t == TT::KwAlwaysFf || t == TT::KwAlwaysLatch) {
                parse_always_block(unit);
                continue;
            }

            // Case statements (standalone, outside always — rare but possible)
            if (t == TT::KwCase || t == TT::KwCasex || t == TT::KwCasez) {
                parse_case_statement(unit);
                continue;
            }

            // Signal declarations
            if (t == TT::KwWire || t == TT::KwReg || t == TT::KwLogic ||
                t == TT::KwInteger || t == TT::KwReal || t == TT::KwTime) {
                parse_signal_decl(unit);
                continue;
            }

            // Generate blocks
            if (t == TT::KwGenerate) {
                parse_generate_block(unit);
                continue;
            }

            // Labeled begin blocks
            if (t == TT::KwBegin) {
                parse_labeled_block(unit);
                continue;
            }

            // Defparam detection
            if (t == TT::KwDefparam) {
                unit.has_defparam = true;
                skip_to_semicolon();
                continue;
            }

            // Skip everything else
            advance();
        }

        // Reached end without finding end keyword
        unit.end_line = tokens.back().pos.line;
        diag("missing " + std::string(verilog_token_name(end_kw)));
    }

    // -- Parameter list #(...) ----------------------------------------------

    void parse_parameter_list(DesignUnit& unit) {
        advance(); // consume #
        if (!check(TT::LParen)) return;
        advance(); // consume (

        while (!at_end() && !check(TT::RParen)) {
            // Skip parameter/localparam keyword
            bool is_local = false;
            if (check(TT::KwParameter)) {
                advance();
            } else if (check(TT::KwLocalparam)) {
                is_local = true;
                advance();
            }

            // Skip type keywords (integer, real, etc.)
            skip_type_prefix();

            // Extract name
            if (check(TT::Identifier)) {
                ParamDecl param;
                param.pos = peek().pos;
                param.name = peek().text;
                param.is_localparam = is_local;
                advance();

                // Skip range [...]
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Default value: = expr
                if (match(TT::Assign)) {
                    param.default_text = skip_expression_text();
                }

                unit.params.push_back(std::move(param));
            } else {
                // Skip to comma or close paren
                skip_to_comma_or_close();
            }

            if (!match(TT::Comma)) break;
        }
        match(TT::RParen);
    }

    // -- Port list (...) ----------------------------------------------------

    void parse_port_list(DesignUnit& unit) {
        advance(); // consume (

        // Detect ANSI vs non-ANSI: if first token after ( is a direction keyword
        // or a type keyword, it's ANSI style
        bool is_ansi = check(TT::KwInput) || check(TT::KwOutput) ||
                       check(TT::KwInout) || check(TT::KwRef);

        if (!is_ansi) {
            // Non-ANSI: just port names separated by commas
            // e.g., module foo(a, b, c);
            // Actual declarations come in the body
            while (!at_end() && !check(TT::RParen)) {
                if (check(TT::Identifier)) advance();
                if (!match(TT::Comma)) break;
            }
            match(TT::RParen);
            return;
        }

        // ANSI-style ports
        PortDirection current_dir = PortDirection::Input;
        while (!at_end() && !check(TT::RParen)) {
            // Direction
            if (check(TT::KwInput))  { current_dir = PortDirection::Input;  advance(); }
            else if (check(TT::KwOutput)) { current_dir = PortDirection::Output; advance(); }
            else if (check(TT::KwInout))  { current_dir = PortDirection::Inout;  advance(); }
            else if (check(TT::KwRef))    { current_dir = PortDirection::Ref;    advance(); }

            // Type text (wire, reg, logic, logic [7:0], etc.)
            std::string type_text = collect_type_text();

            // Port name
            if (check(TT::Identifier)) {
                PortDecl port;
                port.pos = peek().pos;
                port.name = peek().text;
                port.direction = current_dir;
                port.type_text = type_text;
                advance();

                // Skip unpacked dimensions [N] after name
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Skip default value = expr
                if (match(TT::Assign)) {
                    skip_expression();
                }

                unit.ports.push_back(std::move(port));
            } else if (check(TT::Identifier) == false && !check(TT::RParen)) {
                // Interface port: axi_if.master axi
                // or just an identifier we didn't catch
                skip_to_comma_or_close();
            }

            if (!match(TT::Comma)) break;
        }
        match(TT::RParen);
    }

    // -- Non-ANSI port declarations in body ---------------------------------

    void parse_non_ansi_port(DesignUnit& unit) {
        PortDirection dir = PortDirection::Input;
        if (check(TT::KwInput))  { dir = PortDirection::Input;  advance(); }
        else if (check(TT::KwOutput)) { dir = PortDirection::Output; advance(); }
        else if (check(TT::KwInout))  { dir = PortDirection::Inout;  advance(); }

        std::string type_text = collect_type_text();

        // One or more port names separated by commas
        while (!at_end() && !check(TT::Semicolon)) {
            if (check(TT::Identifier)) {
                PortDecl port;
                port.pos = peek().pos;
                port.name = peek().text;
                port.direction = dir;
                port.type_text = type_text;
                advance();

                // Skip unpacked dims
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Check for existing port and update direction/type
                bool found = false;
                for (auto& existing : unit.ports) {
                    if (existing.name == port.name) {
                        existing.direction = dir;
                        if (!type_text.empty()) existing.type_text = type_text;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    unit.ports.push_back(std::move(port));
                }
            } else {
                advance(); // skip unexpected token
            }
            if (!match(TT::Comma)) break;
        }
        match(TT::Semicolon);
    }

    // -- Body parameters ----------------------------------------------------

    void parse_body_parameter(DesignUnit& unit) {
        bool is_local = check(TT::KwLocalparam);
        advance(); // consume parameter/localparam

        skip_type_prefix();

        while (!at_end() && !check(TT::Semicolon)) {
            if (check(TT::Identifier)) {
                ParamDecl param;
                param.pos = peek().pos;
                param.name = peek().text;
                param.is_localparam = is_local;
                advance();

                // Skip range
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                if (match(TT::Assign)) {
                    param.default_text = skip_expression_text();
                }

                unit.params.push_back(std::move(param));
            } else {
                advance();
            }
            if (!match(TT::Comma)) break;
        }
        match(TT::Semicolon);
    }

    // -- Import statements --------------------------------------------------

    void parse_import(DesignUnit& unit) {
        advance(); // consume import

        while (!at_end() && !check(TT::Semicolon)) {
            if (check(TT::Identifier)) {
                ImportDecl imp;
                imp.pos = peek().pos;
                imp.package_name = peek().text;
                advance();

                if (match(TT::DoubleColon)) {
                    if (check(TT::Star)) {
                        imp.symbol = "*";
                        imp.is_wildcard = true;
                        advance();
                    } else if (check(TT::Identifier)) {
                        imp.symbol = peek().text;
                        advance();
                    }
                }
                unit.imports.push_back(std::move(imp));
            } else {
                advance();
            }
            if (!match(TT::Comma)) break;
        }
        match(TT::Semicolon);
    }

    // -- Instantiation detection --------------------------------------------

    bool try_parse_instantiation(DesignUnit& unit) {
        // Heuristic: IDENT IDENT ( ... ) ; or IDENT #( ... ) IDENT ( ... ) ;
        // Must not be a keyword
        if (!check(TT::Identifier)) return false;

        auto& t1 = peek();
        auto& t2 = peek_at(1);

        // Pattern 1: IDENT #(  (parameterized instantiation)
        if (t2.type == TT::Hash) {
            auto& t3 = peek_at(2);
            if (t3.type == TT::LParen) {
                Instantiation inst;
                inst.pos = t1.pos;
                inst.module_name = t1.text;
                inst.is_parameterized = true;
                advance(); // module name
                advance(); // #
                skip_balanced(TT::LParen, TT::RParen); // parameter values

                // Now expect instance name
                if (check(TT::Identifier)) {
                    inst.instance_name = peek().text;
                    advance();
                }

                // Skip array dimensions [N]
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Port connections (...)
                if (check(TT::LParen)) {
                    skip_balanced(TT::LParen, TT::RParen);
                }
                match(TT::Semicolon);

                unit.instantiations.push_back(std::move(inst));
                return true;
            }
        }

        // Pattern 2: IDENT IDENT (  (simple instantiation)
        if (t2.type == TT::Identifier) {
            auto& t3 = peek_at(2);
            if (t3.type == TT::LParen || t3.type == TT::LBracket) {
                Instantiation inst;
                inst.pos = t1.pos;
                inst.module_name = t1.text;
                inst.instance_name = t2.text;
                advance(); // module name
                advance(); // instance name

                // Skip array dimensions
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Port connections
                if (check(TT::LParen)) {
                    skip_balanced(TT::LParen, TT::RParen);
                }
                match(TT::Semicolon);

                unit.instantiations.push_back(std::move(inst));
                return true;
            }
        }

        return false;
    }

    // -- Always blocks ------------------------------------------------------

    void parse_always_block(DesignUnit& unit) {
        AlwaysBlock blk;
        blk.pos = peek().pos;

        auto t = peek().type;
        if (t == TT::KwAlwaysComb)  { blk.kind = AlwaysKind::Comb;  advance(); }
        else if (t == TT::KwAlwaysFf)    { blk.kind = AlwaysKind::Ff;    advance(); }
        else if (t == TT::KwAlwaysLatch) { blk.kind = AlwaysKind::Latch; advance(); }
        else {
            advance(); // consume 'always'
            // Check for @(*)
            if (check(TT::At)) {
                advance(); // @
                if (check(TT::LParen)) {
                    advance(); // (
                    if (check(TT::Star)) {
                        blk.kind = AlwaysKind::Star;
                        advance(); // *
                        match(TT::RParen);
                    } else {
                        // Sensitivity list — classify later
                        blk.kind = AlwaysKind::Plain;
                        // Skip to matching )
                        int depth = 1;
                        while (!at_end() && depth > 0) {
                            if (check(TT::LParen)) ++depth;
                            else if (check(TT::RParen)) --depth;
                            if (depth > 0) advance();
                        }
                        if (check(TT::RParen)) advance();
                    }
                }
            }
        }

        // For always_ff: skip @(posedge clk ...) sensitivity list
        if (blk.kind == AlwaysKind::Ff || blk.kind == AlwaysKind::Latch) {
            if (check(TT::At)) {
                advance(); // @
                if (check(TT::LParen)) {
                    skip_balanced(TT::LParen, TT::RParen);
                }
            }
        }

        // Parse begin...end block or single statement
        if (check(TT::KwBegin)) {
            auto begin_pos = peek().pos;
            advance(); // begin

            // Check for label
            if (check(TT::Colon)) {
                advance();
                if (check(TT::Identifier)) {
                    blk.label = peek().text;
                    advance();
                }
            }

            // Parse statements inside
            parse_always_body(unit, blk);
        } else {
            // Single statement
            parse_always_statement(unit, blk);
        }

        unit.always_blocks.push_back(std::move(blk));
    }

    void parse_always_body(DesignUnit& unit, AlwaysBlock& blk) {
        int begin_depth = 1;
        while (!at_end() && begin_depth > 0) {
            auto t = peek().type;

            if (t == TT::KwBegin) {
                ++begin_depth;
                advance();
                // Skip label
                if (check(TT::Colon)) {
                    advance();
                    if (check(TT::Identifier)) advance();
                }
                continue;
            }

            if (t == TT::KwEnd) {
                --begin_depth;
                advance();
                // Skip end label
                if (check(TT::Colon)) {
                    advance();
                    if (check(TT::Identifier)) advance();
                }
                continue;
            }

            // Case statements inside always
            if (t == TT::KwCase || t == TT::KwCasex || t == TT::KwCasez ||
                t == TT::KwUnique || t == TT::KwPriority) {
                parse_case_statement(unit);
                continue;
            }

            // Look for assignments
            parse_always_statement(unit, blk);
        }
    }

    void parse_always_statement(DesignUnit& unit, AlwaysBlock& blk) {
        // Look for: IDENT = expr ; or IDENT <= expr ;
        if (check(TT::Identifier)) {
            auto& id_tok = peek();
            std::string target = id_tok.text;
            SourcePos apos = id_tok.pos;
            advance();

            // Skip array/bit selects [...]
            while (check(TT::LBracket)) {
                skip_balanced(TT::LBracket, TT::RBracket);
            }

            if (check(TT::Assign)) {
                Assignment asn;
                asn.target = target;
                asn.is_blocking = true;
                asn.pos = apos;
                advance(); // =
                skip_to_semicolon();
                blk.assignments.push_back(std::move(asn));
                return;
            } else if (check(TT::NonBlocking)) {
                Assignment asn;
                asn.target = target;
                asn.is_blocking = false;
                asn.pos = apos;
                advance(); // <=
                skip_to_semicolon();
                blk.assignments.push_back(std::move(asn));
                return;
            }
        }

        // If/else or other — just skip to semicolon or handle nesting
        if (check(TT::KwIf)) {
            advance();
            if (check(TT::LParen)) skip_balanced(TT::LParen, TT::RParen);
            return; // will continue parsing statements
        }

        if (!at_end() && !check(TT::KwEnd) && !check(TT::KwBegin)) {
            advance(); // skip unrecognized token
        }
    }

    // -- Case statements ----------------------------------------------------

    void parse_case_statement(DesignUnit& unit) {
        CaseStatement cs;
        cs.pos = peek().pos;

        // Check for unique/priority prefix
        if (check(TT::KwUnique)) {
            cs.is_unique = true;
            advance();
        } else if (check(TT::KwPriority)) {
            cs.is_priority = true;
            advance();
        }

        auto t = peek().type;
        if (t == TT::KwCase)  { cs.kind = CaseKind::Case;  advance(); }
        else if (t == TT::KwCasex) { cs.kind = CaseKind::Casex; advance(); }
        else if (t == TT::KwCasez) { cs.kind = CaseKind::Casez; advance(); }
        else return; // not actually a case statement

        // Skip case expression (...)
        if (check(TT::LParen)) skip_balanced(TT::LParen, TT::RParen);

        // Scan for default and endcase
        int case_depth = 1;
        while (!at_end() && case_depth > 0) {
            auto ct = peek().type;
            if (ct == TT::KwCase || ct == TT::KwCasex || ct == TT::KwCasez) {
                ++case_depth;
            } else if (ct == TT::KwEndcase) {
                --case_depth;
                if (case_depth == 0) {
                    advance();
                    break;
                }
            } else if (ct == TT::KwDefault && case_depth == 1) {
                cs.has_default = true;
            }
            advance();
        }

        unit.case_statements.push_back(std::move(cs));
    }

    // -- Signal declarations ------------------------------------------------

    void parse_signal_decl(DesignUnit& unit) {
        std::string type_text;
        type_text = peek().text;
        advance(); // consume wire/reg/logic/integer/real/time

        // Collect additional type info (signed, ranges)
        std::string extra = collect_type_text();
        if (!extra.empty()) type_text += " " + extra;

        // One or more signal names
        while (!at_end() && !check(TT::Semicolon)) {
            if (check(TT::Identifier)) {
                SignalDecl sig;
                sig.pos = peek().pos;
                sig.name = peek().text;
                sig.type_text = type_text;
                advance();

                // Skip unpacked dims
                while (check(TT::LBracket)) {
                    skip_balanced(TT::LBracket, TT::RBracket);
                }

                // Skip initial assignment
                if (match(TT::Assign)) {
                    skip_expression();
                }

                unit.signals.push_back(std::move(sig));
            } else {
                advance();
            }
            if (!match(TT::Comma)) break;
        }
        match(TT::Semicolon);
    }

    // -- Generate blocks ----------------------------------------------------

    void parse_generate_block(DesignUnit& unit) {
        SourcePos gpos = peek().pos;
        advance(); // consume generate

        // Look for label: for/if with begin : label
        // We scan until endgenerate, tracking labels
        int gen_depth = 1;
        while (!at_end() && gen_depth > 0) {
            auto t = peek().type;
            if (t == TT::KwGenerate) {
                ++gen_depth;
                advance();
                continue;
            }
            if (t == TT::KwEndgenerate) {
                --gen_depth;
                advance();
                continue;
            }

            // Detect begin : label inside generate
            if (t == TT::KwBegin) {
                advance();
                if (check(TT::Colon)) {
                    advance();
                    if (check(TT::Identifier)) {
                        GenerateBlock gb;
                        gb.pos = gpos;
                        gb.label = peek().text;
                        gb.has_label = true;
                        unit.generate_blocks.push_back(std::move(gb));
                        advance();
                    }
                } else {
                    // Unlabeled generate block
                    GenerateBlock gb;
                    gb.pos = gpos;
                    gb.has_label = false;
                    unit.generate_blocks.push_back(std::move(gb));
                }
                continue;
            }

            // Nested design units inside generate
            if (t == TT::KwModule) {
                parse_design_unit(DesignUnitKind::Module, TT::KwEndmodule);
                continue;
            }

            // Instantiations inside generate
            if (t == TT::Identifier) {
                if (try_parse_instantiation(unit)) continue;
            }

            advance();
        }
    }

    // -- Labeled blocks -----------------------------------------------------

    void parse_labeled_block(DesignUnit& unit) {
        SourcePos bpos = peek().pos;
        advance(); // consume begin

        LabeledBlock lb;
        lb.pos = bpos;

        if (check(TT::Colon)) {
            advance();
            if (check(TT::Identifier)) {
                lb.begin_label = peek().text;
                advance();
            }
        }

        // Skip to matching end
        int bd = 1;
        while (!at_end() && bd > 0) {
            if (check(TT::KwBegin)) ++bd;
            else if (check(TT::KwEnd)) --bd;
            if (bd > 0) advance();
        }

        if (check(TT::KwEnd)) {
            advance();
            if (check(TT::Colon)) {
                advance();
                if (check(TT::Identifier)) {
                    lb.end_label = peek().text;
                    advance();
                }
            }
        }

        lb.labels_match = (lb.begin_label == lb.end_label);

        // Only record if there's at least one label
        if (!lb.begin_label.empty() || !lb.end_label.empty()) {
            unit.labeled_blocks.push_back(std::move(lb));
        }
    }

    // -- Type helpers -------------------------------------------------------

    void skip_type_prefix() {
        // Skip type keywords like integer, real, logic, signed, unsigned, etc.
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::KwInteger || t == TT::KwReal || t == TT::KwTime ||
                t == TT::KwLogic || t == TT::KwBit || t == TT::KwReg ||
                t == TT::KwWire) {
                advance();
            } else if (peek().text == "signed" || peek().text == "unsigned") {
                advance();
            } else if (t == TT::LBracket) {
                // Range like [7:0]
                skip_balanced(TT::LBracket, TT::RBracket);
            } else {
                break;
            }
        }
    }

    std::string collect_type_text() {
        std::string text;
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::KwWire || t == TT::KwReg || t == TT::KwLogic ||
                t == TT::KwBit || t == TT::KwInteger || t == TT::KwReal ||
                t == TT::KwTime) {
                if (!text.empty()) text += " ";
                text += peek().text;
                advance();
            } else if (peek().text == "signed" || peek().text == "unsigned") {
                if (!text.empty()) text += " ";
                text += peek().text;
                advance();
            } else if (t == TT::LBracket) {
                if (!text.empty()) text += " ";
                text += "[";
                advance(); // [
                int bd = 1;
                while (!at_end() && bd > 0) {
                    if (check(TT::LBracket)) ++bd;
                    else if (check(TT::RBracket)) {
                        --bd;
                        if (bd == 0) { advance(); break; }
                    }
                    text += peek().text;
                    advance();
                }
                text += "]";
            } else {
                break;
            }
        }
        return text;
    }

    // -- Expression skipping ------------------------------------------------

    void skip_expression() {
        // Skip to next comma, close paren, or semicolon at depth 0
        int paren = 0, bracket = 0, brace = 0;
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::LParen) ++paren;
            else if (t == TT::RParen) {
                if (paren == 0) return;
                --paren;
            }
            else if (t == TT::LBracket) ++bracket;
            else if (t == TT::RBracket) {
                if (bracket == 0) return;
                --bracket;
            }
            else if (t == TT::LBrace) ++brace;
            else if (t == TT::RBrace) {
                if (brace == 0) return;
                --brace;
            }
            else if ((t == TT::Comma || t == TT::Semicolon) &&
                     paren == 0 && bracket == 0 && brace == 0) {
                return;
            }
            advance();
        }
    }

    std::string skip_expression_text() {
        std::string text;
        int paren = 0, bracket = 0, brace = 0;
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::LParen) ++paren;
            else if (t == TT::RParen) {
                if (paren == 0) break;
                --paren;
            }
            else if (t == TT::LBracket) ++bracket;
            else if (t == TT::RBracket) {
                if (bracket == 0) break;
                --bracket;
            }
            else if (t == TT::LBrace) ++brace;
            else if (t == TT::RBrace) {
                if (brace == 0) break;
                --brace;
            }
            else if ((t == TT::Comma || t == TT::Semicolon) &&
                     paren == 0 && bracket == 0 && brace == 0) {
                break;
            }
            if (!text.empty()) text += " ";
            text += peek().text;
            advance();
        }
        return text;
    }

    void skip_to_comma_or_close() {
        int paren = 0;
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::LParen) ++paren;
            else if (t == TT::RParen) {
                if (paren == 0) return;
                --paren;
            }
            else if (t == TT::Comma && paren == 0) return;
            else if (t == TT::Semicolon) return;
            advance();
        }
    }

    // -- Error recovery -----------------------------------------------------

    void skip_to_next_structural() {
        while (!at_end()) {
            auto t = peek().type;
            if (t == TT::KwModule || t == TT::KwPackage ||
                t == TT::KwInterface || t == TT::KwClass ||
                t == TT::KwProgram) {
                return;
            }
            advance();
        }
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<ParseResult> parse(const LexResult& lex_result,
                          const std::string& filename,
                          bool is_sv) {
    if (lex_result.tokens.empty()) {
        return Result<ParseResult>::ok(ParseResult{});
    }

    Parser parser(lex_result.tokens, filename, is_sv);
    parser.parse_file();
    return Result<ParseResult>::ok(std::move(parser.result));
}

} // namespace loom
