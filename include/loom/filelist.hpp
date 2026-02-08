#pragma once

#include <loom/result.hpp>
#include <loom/target_expr.hpp>
#include <loom/lang/ir.hpp>
#include <loom/build_cache.hpp>
#include <loom/graph.hpp>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace loom {

struct Project;  // forward decl

enum class FilelistFormat { DotF, Json };

struct FilelistOptions {
    TargetSet active_targets;
    std::optional<std::string> top_module;       // Override auto-detect
    FilelistFormat format = FilelistFormat::DotF;
    bool include_testbenches = false;
    bool warn_black_boxes = true;
};

struct FilelistEntry {
    std::string file_path;                       // Absolute path
    std::vector<std::string> defines;
    std::vector<std::string> include_dirs;
};

struct FilelistResult {
    std::vector<FilelistEntry> files;            // Providers-first order
    std::vector<std::string> top_modules;
    std::vector<std::string> testbench_modules;
    std::vector<std::string> black_boxes;        // Instantiated but undefined
    std::string to_dot_f() const;
    std::string to_json() const;
};

class FilelistGenerator {
public:
    explicit FilelistGenerator(BuildCache* cache = nullptr);

    // Full pipeline: project + target filtering → sorted filelist
    Result<FilelistResult> generate(
        const Project& project,
        const FilelistOptions& options = {});

    // From pre-collected source groups (main entry for testing)
    Result<FilelistResult> generate_from_groups(
        const std::vector<SourceGroup>& groups,
        const FilelistOptions& options = {});

private:
    BuildCache* cache_;

    // Internal: unit info for graph building
    struct UnitInfo {
        std::string name;
        DesignUnitKind kind;
        std::string file_path;
        bool has_ports;
    };

    // Pipeline steps
    Result<std::unordered_map<std::string, ParseResult>> parse_all_files(
        const std::vector<SourceGroup>& groups);

    void build_unit_graph(
        const std::unordered_map<std::string, ParseResult>& parse_results,
        GraphMap<>& unit_graph,
        std::unordered_map<std::string, UnitInfo>& unit_map);

    void build_file_graph(
        const GraphMap<>& unit_graph,
        const std::unordered_map<std::string, UnitInfo>& unit_map,
        GraphMap<>& file_graph);

    std::vector<std::string> detect_top_modules(
        const GraphMap<>& unit_graph,
        const std::unordered_map<std::string, UnitInfo>& unit_map);

    std::vector<std::string> detect_black_boxes(
        const GraphMap<>& unit_graph,
        const std::unordered_map<std::string, UnitInfo>& unit_map);

    std::vector<std::string> detect_testbenches(
        const std::unordered_map<std::string, UnitInfo>& unit_map);

    static bool is_testbench(const UnitInfo& info);
};

} // namespace loom
