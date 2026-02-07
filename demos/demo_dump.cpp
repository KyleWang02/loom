#include <loom/lang/parser.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace loom;

static const char* direction_str(PortDirection d) {
    switch (d) {
    case PortDirection::Input:  return "input";
    case PortDirection::Output: return "output";
    case PortDirection::Inout:  return "inout";
    case PortDirection::Ref:    return "ref";
    }
    return "?";
}

static const char* unit_kind_str(DesignUnitKind k) {
    switch (k) {
    case DesignUnitKind::Module:    return "module";
    case DesignUnitKind::Package:   return "package";
    case DesignUnitKind::Interface: return "interface";
    case DesignUnitKind::Class:     return "class";
    case DesignUnitKind::Program:   return "program";
    }
    return "?";
}

static const char* always_kind_str(AlwaysKind k) {
    switch (k) {
    case AlwaysKind::Plain: return "always @(...)";
    case AlwaysKind::Star:  return "always @(*)";
    case AlwaysKind::Comb:  return "always_comb";
    case AlwaysKind::Ff:    return "always_ff";
    case AlwaysKind::Latch: return "always_latch";
    }
    return "?";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: loom-dump <file.v|file.sv> [--tokens]\n";
        return 1;
    }

    std::string path = argv[1];
    bool show_tokens = (argc > 2 && std::string(argv[2]) == "--tokens");
    bool is_sv = path.size() >= 3 && path.substr(path.size() - 3) == ".sv";

    std::ifstream f(path);
    if (!f) {
        std::cerr << "error: cannot open " << path << "\n";
        return 1;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string source = ss.str();

    // Lex
    auto lr = lex(source, path, is_sv);
    if (lr.is_err()) {
        std::cerr << "lex error: " << lr.error().message << "\n";
        return 1;
    }

    auto& lex_result = lr.value();
    std::cout << "--- " << path << " ---\n";
    std::cout << "Tokens: " << lex_result.tokens.size()
              << "  Comments: " << lex_result.comments.size() << "\n";

    if (show_tokens) {
        std::cout << "\n-- Tokens --\n";
        for (auto& t : lex_result.tokens) {
            std::cout << "  " << t.pos.line << ":" << t.pos.col
                      << "  " << verilog_token_name(t.type)
                      << "  \"" << t.text << "\"\n";
        }
    }

    // Parse
    auto pr = parse(lex_result, path, is_sv);
    if (pr.is_err()) {
        std::cerr << "parse error: " << pr.error().message << "\n";
        return 1;
    }

    auto& result = pr.value();
    std::cout << "Design units: " << result.units.size() << "\n\n";

    for (auto& u : result.units) {
        std::string indent(u.depth * 2, ' ');
        std::cout << indent << unit_kind_str(u.kind) << " " << u.name
                  << "  (lines " << u.start_line << "-" << u.end_line << ")\n";

        if (!u.params.empty()) {
            std::cout << indent << "  Parameters:\n";
            for (auto& p : u.params) {
                std::cout << indent << "    " << p.name;
                if (!p.default_text.empty()) std::cout << " = " << p.default_text;
                if (p.is_localparam) std::cout << "  [localparam]";
                std::cout << "\n";
            }
        }

        if (!u.ports.empty()) {
            std::cout << indent << "  Ports:\n";
            for (auto& p : u.ports) {
                std::cout << indent << "    " << direction_str(p.direction)
                          << " " << p.name;
                if (!p.type_text.empty()) std::cout << " : " << p.type_text;
                std::cout << "\n";
            }
        }

        if (!u.imports.empty()) {
            std::cout << indent << "  Imports:\n";
            for (auto& imp : u.imports) {
                std::cout << indent << "    " << imp.package_name
                          << "::" << imp.symbol << "\n";
            }
        }

        if (!u.instantiations.empty()) {
            std::cout << indent << "  Instantiations:\n";
            for (auto& inst : u.instantiations) {
                std::cout << indent << "    " << inst.module_name
                          << " " << inst.instance_name;
                if (inst.is_parameterized) std::cout << " [parameterized]";
                std::cout << "\n";
            }
        }

        if (!u.always_blocks.empty()) {
            std::cout << indent << "  Always blocks:\n";
            for (auto& ab : u.always_blocks) {
                std::cout << indent << "    " << always_kind_str(ab.kind);
                if (!ab.label.empty()) std::cout << " : " << ab.label;
                if (!ab.assignments.empty()) {
                    std::cout << "  (" << ab.assignments.size() << " assignments:";
                    for (auto& a : ab.assignments) {
                        std::cout << " " << a.target
                                  << (a.is_blocking ? "=" : "<=");
                    }
                    std::cout << ")";
                }
                std::cout << "\n";
            }
        }

        if (!u.signals.empty()) {
            std::cout << indent << "  Signals:\n";
            for (auto& s : u.signals) {
                std::cout << indent << "    " << s.type_text << " " << s.name << "\n";
            }
        }

        if (!u.case_statements.empty()) {
            std::cout << indent << "  Case statements: " << u.case_statements.size();
            for (auto& cs : u.case_statements) {
                std::cout << " [";
                if (cs.is_unique) std::cout << "unique ";
                if (cs.is_priority) std::cout << "priority ";
                std::cout << (cs.kind == CaseKind::Case ? "case" :
                             cs.kind == CaseKind::Casex ? "casex" : "casez");
                std::cout << (cs.has_default ? "+default" : "-default");
                std::cout << "]";
            }
            std::cout << "\n";
        }

        if (!u.generate_blocks.empty()) {
            std::cout << indent << "  Generate blocks:\n";
            for (auto& gb : u.generate_blocks) {
                std::cout << indent << "    "
                          << (gb.has_label ? gb.label : "(unlabeled)") << "\n";
            }
        }

        if (u.has_defparam) {
            std::cout << indent << "  [uses defparam]\n";
        }

        std::cout << "\n";
    }

    if (!result.diagnostics.empty()) {
        std::cout << "-- Diagnostics --\n";
        for (auto& d : result.diagnostics) {
            std::cout << "  " << d.file << ":" << d.line << ":" << d.col
                      << ": " << d.message << "\n";
        }
    }

    return 0;
}
