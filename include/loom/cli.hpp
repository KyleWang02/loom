#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace loom {

// Levenshtein distance for fuzzy command matching
size_t levenshtein(const std::string& a, const std::string& b);

// A command-line flag definition
struct Flag {
    std::string long_name;       // e.g. "verbose"
    std::string short_name;      // e.g. "v" (empty if none)
    std::string description;
    bool takes_value = false;
    std::string value_name;      // displayed in help, e.g. "FILE"
    std::string default_value;
    bool repeatable = false;     // can appear multiple times
};

// Parsed command-line arguments
class CliArgs {
public:
    // Check if a flag was provided
    bool has(const std::string& name) const;

    // Get the value of a valued flag (empty if not set)
    std::string get(const std::string& name) const;

    // Get all values for a repeatable flag
    std::vector<std::string> get_all(const std::string& name) const;

    // Count occurrences of a flag (useful for -v -v -v)
    int count(const std::string& name) const;

    // Positional arguments (non-flag, non-subcommand)
    const std::vector<std::string>& positional() const;

    // Pass-through arguments (after --)
    const std::vector<std::string>& passthrough() const;

    // Builder methods (used by parser)
    void set_flag(const std::string& name);
    void set_value(const std::string& name, const std::string& value);
    void add_value(const std::string& name, const std::string& value);
    void add_positional(const std::string& arg);
    void add_passthrough(const std::string& arg);
    void increment(const std::string& name);

private:
    std::unordered_map<std::string, std::vector<std::string>> values_;
    std::unordered_map<std::string, int> counts_;
    std::vector<std::string> positional_;
    std::vector<std::string> passthrough_;
};

// A CLI subcommand
struct Command {
    std::string name;
    std::string summary;         // one-line for listing
    std::string description;     // detailed help
    std::string usage;           // e.g. "loom build [flags] [-- ...]"
    std::string group;           // for grouped help: "Project", "Dependencies", etc.
    std::vector<Flag> flags;

    // handler(global_args, command_args) -> exit code
    std::function<Result<int>(CliArgs&, CliArgs&)> handler;

    // Generate per-command help text
    std::string help_text() const;
};

// Top-level CLI parser
class CliParser {
public:
    CliParser(const std::string& name, const std::string& version);

    void add_global_flag(Flag flag);
    void add_command(Command cmd);

    // Parse and dispatch. Returns exit code.
    Result<int> run(int argc, char* argv[]);

    // Generate top-level help text
    std::string help_text() const;

private:
    std::string name_;
    std::string version_;
    std::vector<Flag> global_flags_;
    std::vector<Command> commands_;

    // Parse flags from argv into args, starting at idx. Returns index of first non-flag.
    Result<int> parse_flags(int argc, char* argv[], int start,
                            const std::vector<Flag>& flags, CliArgs& args);

    // Find a command by name (nullptr if not found)
    Command* find_command(const std::string& name);

    // Suggest a similar command name
    std::string suggest_command(const std::string& name) const;
};

// Forward declarations for command registration
void register_new(CliParser& cli);
void register_init(CliParser& cli);
void register_info(CliParser& cli);
void register_env(CliParser& cli);
void register_config(CliParser& cli);
void register_lock(CliParser& cli);
void register_update(CliParser& cli);
void register_tree(CliParser& cli);
void register_clean(CliParser& cli);
void register_build(CliParser& cli);
void register_plan(CliParser& cli);
void register_lint(CliParser& cli);
void register_doc(CliParser& cli);

} // namespace loom
