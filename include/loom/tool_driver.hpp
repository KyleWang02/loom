#pragma once

#include <loom/tool_types.hpp>
#include <loom/filelist.hpp>
#include <loom/manifest.hpp>
#include <loom/swap.hpp>
#include <memory>
#include <unordered_set>

namespace loom {

// Base class for all EDA tool drivers.
// Separates command generation (pure, testable) from execution (subprocess).
class ToolDriver {
public:
    virtual ~ToolDriver() = default;

    // Identity
    virtual std::string name() const = 0;
    virtual std::string display_name() const = 0;
    virtual std::unordered_set<ToolAction> supported_actions() const = 0;
    bool supports(ToolAction action) const;

    // Discovery
    virtual std::string executable_name() const = 0;
    Result<std::string> find_executable() const;
    Result<std::string> detect_version() const;

    // Command generation (testable without real tools)
    virtual Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist,
        const ToolOptions& options,
        const std::string& work_dir) const = 0;

    // Execution (runs real commands)
    Result<ToolResult> execute(
        const FilelistResult& filelist,
        const TargetConfig& target,
        const std::string& build_root);

    // Helpers
    static Result<std::string> write_filelist(const FilelistResult& filelist,
                                               const std::string& path);
    static Result<std::string> write_tcl_script(const std::string& content,
                                                 const std::string& path);

protected:
    std::string resolve_top_module(const FilelistResult& filelist,
                                    const ToolOptions& options) const;
};

// ---- Built-in drivers ----

class IcarusDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class VerilatorDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class VivadoSimDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class VivadoSynthDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class QuartusDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class ModelSimDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class VcsDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class XceliumDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class YosysDriver : public ToolDriver {
public:
    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;
};

class CustomDriver : public ToolDriver {
public:
    explicit CustomDriver(const TargetConfig& target);

    std::string name() const override;
    std::string display_name() const override;
    std::string executable_name() const override;
    std::unordered_set<ToolAction> supported_actions() const override;
    Result<std::vector<ToolCommand>> generate_commands(
        const FilelistResult& filelist, const ToolOptions& options,
        const std::string& work_dir) const override;

private:
    TargetConfig target_;
};

// ---- Factory functions ----

// Create a driver by tool name (e.g. "icarus", "verilator", "vivado-synth").
// Returns nullptr if name is unknown.
std::unique_ptr<ToolDriver> create_driver(const std::string& tool_name);

// Create a driver from TargetConfig. Handles "custom" tool type.
std::unique_ptr<ToolDriver> create_driver(const TargetConfig& target);

// List all built-in driver names.
std::vector<std::string> available_drivers();

// Auto-detect the best available driver for an action by searching PATH.
Result<std::unique_ptr<ToolDriver>> detect_driver(ToolAction action);

} // namespace loom
