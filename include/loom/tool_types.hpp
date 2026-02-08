#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace loom {

enum class ToolAction { Lint, Simulate, Synthesize, Build };

Result<ToolAction> parse_tool_action(const std::string& s);
std::string tool_action_name(ToolAction a);

struct ToolResult {
    int exit_code = -1;
    std::string stdout_log;
    std::string stderr_log;
    std::string work_dir;
    std::vector<std::string> artifacts;
};

struct ToolOptions {
    std::vector<std::string> compile_args;
    std::vector<std::string> elaborate_args;
    std::vector<std::string> simulate_args;
    std::vector<std::string> synth_args;
    std::string timescale;           // e.g. "1ns/1ps"
    bool waveform = false;
    std::string waveform_format;     // "vcd", "fst", "fsdb"
    std::string device;              // FPGA part number
    std::string family;              // FPGA family
    std::string top_module;          // explicit override
    std::unordered_map<std::string, std::string> extra;

    static ToolOptions from_map(const std::unordered_map<std::string, std::string>& map);
};

struct ToolCommand {
    std::vector<std::string> args;
    std::string working_dir;
    std::string description;         // human-readable label
};

} // namespace loom
