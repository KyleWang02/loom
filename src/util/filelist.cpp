#include <loom/filelist.hpp>
#include <loom/project.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <loom/sha256.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// FilelistResult output formatters
// ---------------------------------------------------------------------------

std::string FilelistResult::to_dot_f() const {
    std::ostringstream out;

    // Collect all unique include dirs and defines across entries
    std::vector<std::string> all_incdirs;
    std::vector<std::string> all_defines;
    std::unordered_set<std::string> seen_incdirs;
    std::unordered_set<std::string> seen_defines;

    for (const auto& entry : files) {
        for (const auto& dir : entry.include_dirs) {
            if (seen_incdirs.insert(dir).second) {
                all_incdirs.push_back(dir);
            }
        }
        for (const auto& def : entry.defines) {
            if (seen_defines.insert(def).second) {
                all_defines.push_back(def);
            }
        }
    }

    // Write include dirs
    for (const auto& dir : all_incdirs) {
        out << "+incdir+" << dir << "\n";
    }

    // Write defines
    for (const auto& def : all_defines) {
        out << "+define+" << def << "\n";
    }

    // Write file paths
    for (const auto& entry : files) {
        out << entry.file_path << "\n";
    }

    return out.str();
}

std::string FilelistResult::to_json() const {
    std::ostringstream out;
    out << "{\n";

    // files array
    out << "  \"files\": [\n";
    for (size_t i = 0; i < files.size(); ++i) {
        out << "    \"" << files[i].file_path << "\"";
        if (i + 1 < files.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // top_modules
    out << "  \"top_modules\": [";
    for (size_t i = 0; i < top_modules.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << top_modules[i] << "\"";
    }
    out << "],\n";

    // black_boxes
    out << "  \"black_boxes\": [";
    for (size_t i = 0; i < black_boxes.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << black_boxes[i] << "\"";
    }
    out << "]\n";

    out << "}\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// FilelistGenerator
// ---------------------------------------------------------------------------

FilelistGenerator::FilelistGenerator(BuildCache* cache)
    : cache_(cache) {}

Result<FilelistResult> FilelistGenerator::generate(
    const Project& project,
    const FilelistOptions& options)
{
    auto groups_r = project.collect_source_groups(options.active_targets);
    if (groups_r.is_err()) return std::move(groups_r).error();

    return generate_from_groups(groups_r.value(), options);
}

Result<FilelistResult> FilelistGenerator::generate_from_groups(
    const std::vector<SourceGroup>& groups,
    const FilelistOptions& options)
{
    // Step 1: Filter by active targets
    auto filtered = filter_source_groups(groups, options.active_targets);

    // Step 2: Parse all files
    auto parse_map_r = parse_all_files(filtered);
    if (parse_map_r.is_err()) return std::move(parse_map_r).error();
    auto& parse_map = parse_map_r.value();

    // Step 3: Build unit graph
    GraphMap<> unit_graph;
    std::unordered_map<std::string, UnitInfo> unit_map;
    build_unit_graph(parse_map, unit_graph, unit_map);

    // Step 4: Build file graph
    GraphMap<> file_graph;
    build_file_graph(unit_graph, unit_map, file_graph);

    // Also add files that have no design units (define-only files)
    // so they appear in the file graph as isolated nodes
    std::unordered_set<std::string> files_with_units;
    for (const auto& [name, info] : unit_map) {
        files_with_units.insert(info.file_path);
    }

    std::vector<std::string> define_only_files;
    for (const auto& [path, pr] : parse_map) {
        // Only top-level units count
        bool has_top_level = false;
        for (const auto& u : pr.units) {
            if (u.depth == 0) { has_top_level = true; break; }
        }
        if (!has_top_level) {
            define_only_files.push_back(path);
            file_graph.add_node(path);
        }
    }

    // Step 5: Topological sort
    auto topo_r = file_graph.topological_sort();
    if (topo_r.is_err()) return std::move(topo_r).error();

    auto& topo_order = topo_r.value();

    // Kahn's produces dependents-first; reverse for providers-first
    std::reverse(topo_order.begin(), topo_order.end());

    // Move define-only files to front
    std::unordered_set<std::string> define_set(
        define_only_files.begin(), define_only_files.end());
    std::vector<std::string> final_order;
    final_order.reserve(topo_order.size());

    // First: define-only files (in their topo order)
    for (const auto& f : topo_order) {
        if (define_set.count(f)) final_order.push_back(f);
    }
    // Then: everything else
    for (const auto& f : topo_order) {
        if (!define_set.count(f)) final_order.push_back(f);
    }

    // Build a lookup from file_path → source group info for defines/incdirs
    std::unordered_map<std::string, const SourceGroup*> file_to_group;
    for (const auto& g : filtered) {
        for (const auto& f : g.files) {
            file_to_group[f] = &g;
        }
    }

    // Step 6: Detection
    auto top_mods = detect_top_modules(unit_graph, unit_map);
    auto bboxes = detect_black_boxes(unit_graph, unit_map);
    auto tbs = detect_testbenches(unit_map);

    // If user specified a top module, validate and use it
    if (options.top_module.has_value()) {
        top_mods = { options.top_module.value() };
    }

    // Filter testbenches from output if not requested
    std::unordered_set<std::string> tb_set(tbs.begin(), tbs.end());
    std::unordered_set<std::string> tb_files;
    if (!options.include_testbenches) {
        for (const auto& [name, info] : unit_map) {
            if (tb_set.count(name)) {
                tb_files.insert(info.file_path);
            }
        }
    }

    // Build result
    FilelistResult result;
    result.top_modules = std::move(top_mods);
    result.black_boxes = std::move(bboxes);
    result.testbench_modules = std::move(tbs);

    for (const auto& path : final_order) {
        if (!options.include_testbenches && tb_files.count(path)) continue;

        FilelistEntry entry;
        entry.file_path = path;
        auto it = file_to_group.find(path);
        if (it != file_to_group.end()) {
            entry.defines = it->second->defines;
            entry.include_dirs = it->second->include_dirs;
        }
        result.files.push_back(std::move(entry));
    }

    return Result<FilelistResult>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Parse all files
// ---------------------------------------------------------------------------

Result<std::unordered_map<std::string, ParseResult>>
FilelistGenerator::parse_all_files(const std::vector<SourceGroup>& groups)
{
    std::unordered_map<std::string, ParseResult> results;

    for (const auto& group : groups) {
        for (const auto& file_path : group.files) {
            if (results.count(file_path)) continue;  // already parsed

            // Determine if SystemVerilog
            std::string ext = fs::path(file_path).extension().string();
            bool is_sv = (ext == ".sv");

            // Read file contents
            std::ifstream ifs(file_path);
            if (!ifs.is_open()) {
                return LoomError{LoomError::IO,
                    "cannot open source file: " + file_path};
            }
            std::ostringstream ss;
            ss << ifs.rdbuf();
            std::string source = ss.str();

            // Check build cache
            if (cache_) {
                std::string hash = SHA256::hash_hex(source);
                auto cached = cache_->lookup_parse(hash);
                if (cached.is_ok()) {
                    results[file_path] = std::move(cached.value());
                    continue;
                }
            }

            // Lex
            auto lex_r = lex(source, file_path, is_sv);
            if (lex_r.is_err()) return std::move(lex_r).error();

            // Parse
            auto parse_r = parse(lex_r.value(), file_path, is_sv);
            if (parse_r.is_err()) return std::move(parse_r).error();

            // Store in cache
            if (cache_) {
                std::string hash = SHA256::hash_hex(source);
                (void)cache_->store_parse(hash, parse_r.value());
            }

            results[file_path] = std::move(parse_r.value());
        }
    }

    return Result<std::unordered_map<std::string, ParseResult>>::ok(
        std::move(results));
}

// ---------------------------------------------------------------------------
// Build unit graph
// ---------------------------------------------------------------------------

void FilelistGenerator::build_unit_graph(
    const std::unordered_map<std::string, ParseResult>& parse_results,
    GraphMap<>& unit_graph,
    std::unordered_map<std::string, UnitInfo>& unit_map)
{
    // First pass: register all top-level units
    for (const auto& [path, pr] : parse_results) {
        for (const auto& unit : pr.units) {
            if (unit.depth != 0) continue;  // skip nested units

            UnitInfo info;
            info.name = unit.name;
            info.kind = unit.kind;
            info.file_path = path;
            info.has_ports = !unit.ports.empty();

            unit_map[unit.name] = info;
            unit_graph.add_node(unit.name);
        }
    }

    // Second pass: add edges for instantiations and imports
    for (const auto& [path, pr] : parse_results) {
        for (const auto& unit : pr.units) {
            if (unit.depth != 0) continue;

            for (const auto& inst : unit.instantiations) {
                // add_edge auto-creates the target node if undefined (black box)
                unit_graph.add_edge(unit.name, inst.module_name);
            }

            for (const auto& imp : unit.imports) {
                unit_graph.add_edge(unit.name, imp.package_name);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Build file graph
// ---------------------------------------------------------------------------

void FilelistGenerator::build_file_graph(
    const GraphMap<>& unit_graph,
    const std::unordered_map<std::string, UnitInfo>& unit_map,
    GraphMap<>& file_graph)
{
    // Add all files that contain units as nodes
    for (const auto& [name, info] : unit_map) {
        file_graph.add_node(info.file_path);
    }

    // Map unit edges to file edges
    auto& inner = unit_graph.inner();
    for (size_t i = 0; i < inner.node_count(); ++i) {
        const std::string& from_unit = inner.node(i);
        auto from_it = unit_map.find(from_unit);
        if (from_it == unit_map.end()) continue;  // black box source (shouldn't happen)

        const std::string& from_file = from_it->second.file_path;

        for (const auto& edge : inner.successors(i)) {
            const std::string& to_unit = inner.node(edge.to);
            auto to_it = unit_map.find(to_unit);
            if (to_it == unit_map.end()) continue;  // black box target

            const std::string& to_file = to_it->second.file_path;
            if (from_file == to_file) continue;  // skip self-edges

            // Only add if not already present
            if (!file_graph.inner().has_edge(
                    file_graph.node_id(from_file),
                    file_graph.node_id(to_file))) {
                file_graph.add_edge(from_file, to_file);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Detection helpers
// ---------------------------------------------------------------------------

std::vector<std::string> FilelistGenerator::detect_top_modules(
    const GraphMap<>& unit_graph,
    const std::unordered_map<std::string, UnitInfo>& unit_map)
{
    std::vector<std::string> tops;
    auto& inner = unit_graph.inner();

    for (const auto& [name, info] : unit_map) {
        // Only Modules and Programs can be top-level
        if (info.kind == DesignUnitKind::Package ||
            info.kind == DesignUnitKind::Interface ||
            info.kind == DesignUnitKind::Class) {
            continue;
        }

        // Top-level = nobody instantiates this module (in-degree 0)
        auto id = unit_graph.node_id(name);
        if (inner.in_degree(id) == 0) {
            tops.push_back(name);
        }
    }

    std::sort(tops.begin(), tops.end());
    return tops;
}

std::vector<std::string> FilelistGenerator::detect_black_boxes(
    const GraphMap<>& unit_graph,
    const std::unordered_map<std::string, UnitInfo>& unit_map)
{
    std::vector<std::string> bboxes;
    auto& inner = unit_graph.inner();

    for (size_t i = 0; i < inner.node_count(); ++i) {
        const std::string& name = inner.node(i);
        if (unit_map.find(name) == unit_map.end()) {
            bboxes.push_back(name);
        }
    }

    std::sort(bboxes.begin(), bboxes.end());
    return bboxes;
}

std::vector<std::string> FilelistGenerator::detect_testbenches(
    const std::unordered_map<std::string, UnitInfo>& unit_map)
{
    std::vector<std::string> tbs;
    for (const auto& [name, info] : unit_map) {
        if (is_testbench(info)) {
            tbs.push_back(name);
        }
    }
    std::sort(tbs.begin(), tbs.end());
    return tbs;
}

bool FilelistGenerator::is_testbench(const UnitInfo& info) {
    // Heuristic 1: Program kind
    if (info.kind == DesignUnitKind::Program) return true;

    // Heuristic 2: Name contains tb/test/bench (case-insensitive)
    std::string lower_name = info.name;
    std::transform(lower_name.begin(), lower_name.end(),
                   lower_name.begin(), ::tolower);
    if (lower_name.find("tb") != std::string::npos ||
        lower_name.find("test") != std::string::npos ||
        lower_name.find("bench") != std::string::npos) {
        return true;
    }

    // Heuristic 3: No ports (testbenches typically have no I/O)
    if (!info.has_ports && info.kind == DesignUnitKind::Module) {
        return true;
    }

    // Heuristic 4: File path contains tb/, test/, or sim/ directory
    std::string lower_path = info.file_path;
    std::transform(lower_path.begin(), lower_path.end(),
                   lower_path.begin(), ::tolower);
    if (lower_path.find("/tb/") != std::string::npos ||
        lower_path.find("/test/") != std::string::npos ||
        lower_path.find("/sim/") != std::string::npos) {
        return true;
    }

    return false;
}

} // namespace loom
