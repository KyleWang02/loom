#include <loom/sr.hpp>
#include <loom/sha256.hpp>
#include <algorithm>
#include <sstream>

namespace loom {

// ---- Collision detection ----

std::vector<SymbolCollision> SymbolRemapper::detect_collisions(
    const std::unordered_map<std::string, ParseResult>& parse_results) {

    // Build symbol table: name -> list of (file, kind, line)
    std::unordered_map<std::string, std::vector<SymbolOrigin>> symbol_table;

    for (auto& [file, pr] : parse_results) {
        for (auto& unit : pr.units) {
            if (unit.depth != 0) continue;  // skip nested units
            symbol_table[unit.name].push_back({file, unit.kind, unit.start_line});
        }
    }

    // Collect collisions (names with more than one definition)
    std::vector<SymbolCollision> collisions;
    for (auto& [name, origins] : symbol_table) {
        if (origins.size() > 1) {
            // Sort origins by file path for deterministic output
            auto sorted = origins;
            std::sort(sorted.begin(), sorted.end(),
                [](const SymbolOrigin& a, const SymbolOrigin& b) {
                    return a.file < b.file;
                });
            collisions.push_back({name, std::move(sorted)});
        }
    }

    // Sort collisions by name for deterministic ordering
    std::sort(collisions.begin(), collisions.end(),
        [](const SymbolCollision& a, const SymbolCollision& b) {
            return a.name < b.name;
        });

    return collisions;
}

// ---- Name mangling ----

std::string SymbolRemapper::mangle(const std::string& name,
                                    const std::string& qualifier) {
    std::string input = qualifier + ":" + name;
    std::string hash = SHA256::hash_hex(input);
    return name + "_" + hash.substr(0, 8);
}

// ---- Remap table construction ----

FileRemapTable SymbolRemapper::build_remap_table(
    const std::vector<SymbolCollision>& collisions) {

    FileRemapTable table;

    for (auto& collision : collisions) {
        for (auto& origin : collision.origins) {
            std::string mangled = mangle(collision.name, origin.file);
            table[origin.file][collision.name] = mangled;
        }
    }

    return table;
}

// ---- Token stream remapping ----

void SymbolRemapper::remap_tokens(
    std::vector<VerilogToken>& tokens,
    const SymbolMap& mapping) {

    for (auto& tok : tokens) {
        if (tok.type == VerilogTokenType::Identifier) {
            auto it = mapping.find(tok.text);
            if (it != mapping.end()) {
                tok.text = it->second;
            }
        }
    }
}

// ---- Parse result remapping ----

void SymbolRemapper::remap_parse_result(
    ParseResult& result,
    const SymbolMap& mapping) {

    for (auto& unit : result.units) {
        // Remap unit name if it matches a collision
        auto it = mapping.find(unit.name);
        if (it != mapping.end()) {
            unit.name = it->second;
        }

        // Remap instantiation references
        for (auto& inst : unit.instantiations) {
            auto jt = mapping.find(inst.module_name);
            if (jt != mapping.end()) {
                inst.module_name = jt->second;
            }
        }

        // Remap import package references
        for (auto& imp : unit.imports) {
            auto jt = mapping.find(imp.package_name);
            if (jt != mapping.end()) {
                imp.package_name = jt->second;
            }
        }
    }
}

// ---- Report formatting ----

std::string SymbolRemapper::format_report(
    const std::vector<SymbolCollision>& collisions) {

    if (collisions.empty()) return "";

    std::ostringstream os;
    os << "Symbol collisions detected (" << collisions.size() << "):\n\n";

    for (auto& c : collisions) {
        os << "  '" << c.name << "' defined in " << c.origins.size() << " locations:\n";
        for (auto& o : c.origins) {
            os << "    " << o.file << ":" << o.line << "\n";
        }
        os << "\n";
    }

    return os.str();
}

std::string SymbolRemapper::format_json(
    const std::vector<SymbolCollision>& collisions) {

    std::ostringstream os;
    os << "[\n";
    for (size_t i = 0; i < collisions.size(); ++i) {
        auto& c = collisions[i];
        os << "  {\n";
        os << "    \"name\": \"" << c.name << "\",\n";
        os << "    \"origins\": [\n";
        for (size_t j = 0; j < c.origins.size(); ++j) {
            auto& o = c.origins[j];
            os << "      {\"file\": \"" << o.file << "\", \"line\": " << o.line << "}";
            if (j + 1 < c.origins.size()) os << ",";
            os << "\n";
        }
        os << "    ]\n";
        os << "  }";
        if (i + 1 < collisions.size()) os << ",";
        os << "\n";
    }
    os << "]\n";
    return os.str();
}

} // namespace loom
