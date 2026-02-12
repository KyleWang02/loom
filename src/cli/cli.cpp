#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace loom {

// ---- Levenshtein distance ----

size_t levenshtein(const std::string& a, const std::string& b) {
    size_t m = a.size(), n = b.size();
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1));
    for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = j;
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (a[i-1] == b[j-1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }
    return dp[m][n];
}

// ---- CliArgs ----

bool CliArgs::has(const std::string& name) const {
    auto it = counts_.find(name);
    return it != counts_.end() && it->second > 0;
}

std::string CliArgs::get(const std::string& name) const {
    auto it = values_.find(name);
    if (it != values_.end() && !it->second.empty()) {
        return it->second.back();
    }
    return "";
}

std::vector<std::string> CliArgs::get_all(const std::string& name) const {
    auto it = values_.find(name);
    if (it != values_.end()) return it->second;
    return {};
}

int CliArgs::count(const std::string& name) const {
    auto it = counts_.find(name);
    return (it != counts_.end()) ? it->second : 0;
}

const std::vector<std::string>& CliArgs::positional() const {
    return positional_;
}

const std::vector<std::string>& CliArgs::passthrough() const {
    return passthrough_;
}

void CliArgs::set_flag(const std::string& name) {
    counts_[name]++;
}

void CliArgs::set_value(const std::string& name, const std::string& value) {
    values_[name] = {value};
    counts_[name]++;
}

void CliArgs::add_value(const std::string& name, const std::string& value) {
    values_[name].push_back(value);
    counts_[name]++;
}

void CliArgs::add_positional(const std::string& arg) {
    positional_.push_back(arg);
}

void CliArgs::add_passthrough(const std::string& arg) {
    passthrough_.push_back(arg);
}

void CliArgs::increment(const std::string& name) {
    counts_[name]++;
}

// ---- Command ----

std::string Command::help_text() const {
    std::ostringstream os;
    os << summary << "\n\n";
    if (!description.empty()) {
        os << description << "\n\n";
    }
    os << "Usage: " << usage << "\n";
    if (!flags.empty()) {
        os << "\nFlags:\n";
        for (auto& f : flags) {
            os << "  ";
            if (!f.short_name.empty()) {
                os << "-" << f.short_name << ", ";
            } else {
                os << "    ";
            }
            os << "--" << f.long_name;
            if (f.takes_value) {
                os << " <" << (f.value_name.empty() ? "VALUE" : f.value_name) << ">";
            }
            os << "\n        " << f.description;
            if (!f.default_value.empty()) {
                os << " [default: " << f.default_value << "]";
            }
            os << "\n";
        }
    }
    return os.str();
}

// ---- CliParser ----

CliParser::CliParser(const std::string& name, const std::string& version)
    : name_(name), version_(version) {}

void CliParser::add_global_flag(Flag flag) {
    global_flags_.push_back(std::move(flag));
}

void CliParser::add_command(Command cmd) {
    commands_.push_back(std::move(cmd));
}

Result<int> CliParser::run(int argc, char* argv[]) {
    // Two-phase parse: first find the subcommand boundary, then parse each half.
    // Global flags are everything before the first non-flag arg.
    // The first non-flag arg is the subcommand name.
    // Everything after the subcommand is parsed as command-local.

    // Phase 1: scan for the subcommand position
    int subcmd_idx = -1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--") break;
        if (arg.empty() || arg[0] != '-') {
            subcmd_idx = i;
            break;
        }
        // Skip value for known valued flags
        for (auto& f : global_flags_) {
            bool match = false;
            if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
                auto eq = arg.find('=');
                std::string name = (eq != std::string::npos)
                    ? arg.substr(2, eq - 2) : arg.substr(2);
                if (f.long_name == name) match = true;
            } else if (arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
                if (f.short_name == std::string(1, arg[1])) match = true;
            }
            if (match && f.takes_value) {
                // If the value isn't in the same token (no '=' for long flags, no extra chars for short)
                bool has_inline_value = false;
                if (arg.size() > 2 && arg[1] == '-') {
                    has_inline_value = (arg.find('=') != std::string::npos);
                } else if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') {
                    has_inline_value = true; // -tVALUE
                }
                if (!has_inline_value) ++i; // skip next arg (the value)
                break;
            }
        }
    }

    // Parse global flags up to the subcommand
    CliArgs global_args;
    int global_end = (subcmd_idx >= 0) ? subcmd_idx : argc;
    auto idx_r = parse_flags(global_end, argv, 1, global_flags_, global_args);
    if (idx_r.is_err()) return std::move(idx_r).error();

    // Handle --help at top level
    if (global_args.has("help")) {
        std::cout << help_text();
        return Result<int>::ok(0);
    }

    // Handle --version at top level
    if (global_args.has("version")) {
        std::cout << name_ << " " << version_ << "\n";
        return Result<int>::ok(0);
    }

    // No subcommand
    if (subcmd_idx < 0) {
        std::cout << help_text();
        return Result<int>::ok(0);
    }

    // Look up command
    std::string cmd_name = argv[subcmd_idx];
    auto* cmd = find_command(cmd_name);
    if (!cmd) {
        std::string suggestion = suggest_command(cmd_name);
        std::string msg = "unknown command '" + cmd_name + "'";
        if (!suggestion.empty()) {
            msg += "\n\n    did you mean '" + suggestion + "'?";
        }
        return LoomError(LoomError::InvalidArg, msg);
    }

    // Parse command-local flags (everything after the subcommand name)
    CliArgs cmd_args;
    auto cmd_idx_r = parse_flags(argc, argv, subcmd_idx + 1, cmd->flags, cmd_args);
    if (cmd_idx_r.is_err()) return std::move(cmd_idx_r).error();

    // Handle per-command --help
    if (cmd_args.has("help")) {
        std::cout << cmd->help_text();
        return Result<int>::ok(0);
    }

    // Dispatch
    return cmd->handler(global_args, cmd_args);
}

std::string CliParser::help_text() const {
    std::ostringstream os;
    os << name_ << " " << version_ << " - Verilog/SystemVerilog package manager\n\n";
    os << "Usage: " << name_ << " [flags] <command> [args]\n\n";

    // Global flags
    os << "Global Flags:\n";
    for (auto& f : global_flags_) {
        os << "  ";
        if (!f.short_name.empty()) {
            os << "-" << f.short_name << ", ";
        } else {
            os << "    ";
        }
        os << "--" << f.long_name;
        if (f.takes_value) {
            os << " <" << (f.value_name.empty() ? "VALUE" : f.value_name) << ">";
        }
        os << "\n        " << f.description << "\n";
    }
    os << "\n";

    // Group commands
    std::vector<std::string> groups = {"Project", "Dependencies", "Build", "Quality"};
    for (auto& group : groups) {
        bool has_any = false;
        for (auto& c : commands_) {
            if (c.group == group) {
                if (!has_any) {
                    os << group << " Commands:\n";
                    has_any = true;
                }
                os << "  " << c.name;
                // Pad to 14 chars
                for (size_t i = c.name.size(); i < 14; ++i) os << ' ';
                os << c.summary << "\n";
            }
        }
        if (has_any) os << "\n";
    }

    os << "Run '" << name_ << " <command> --help' for more information on a command.\n";
    return os.str();
}

Result<int> CliParser::parse_flags(int argc, char* argv[], int start,
                                    const std::vector<Flag>& flags, CliArgs& args) {
    int i = start;
    while (i < argc) {
        std::string arg = argv[i];

        // Pass-through separator
        if (arg == "--") {
            ++i;
            while (i < argc) {
                args.add_passthrough(argv[i++]);
            }
            break;
        }

        // Not a flag? It's a positional or subcommand
        if (arg.empty() || arg[0] != '-') {
            args.add_positional(arg);
            ++i;
            continue;
        }

        // Long flag: --name or --name=value
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            std::string name;
            std::string value;
            bool has_eq = false;
            auto eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                name = arg.substr(2, eq_pos - 2);
                value = arg.substr(eq_pos + 1);
                has_eq = true;
            } else {
                name = arg.substr(2);
            }

            // Special: --help and --version always recognized
            if (name == "help" || name == "version") {
                args.set_flag(name);
                ++i;
                continue;
            }

            // Find matching flag
            const Flag* match = nullptr;
            for (auto& f : flags) {
                if (f.long_name == name) { match = &f; break; }
            }
            if (!match) {
                return LoomError(LoomError::InvalidArg,
                    "unknown flag '--" + name + "'");
            }

            if (match->takes_value) {
                if (has_eq) {
                    if (match->repeatable) {
                        args.add_value(name, value);
                    } else {
                        args.set_value(name, value);
                    }
                } else {
                    ++i;
                    if (i >= argc) {
                        return LoomError(LoomError::InvalidArg,
                            "flag '--" + name + "' requires a value");
                    }
                    if (match->repeatable) {
                        args.add_value(name, argv[i]);
                    } else {
                        args.set_value(name, argv[i]);
                    }
                }
            } else {
                args.set_flag(name);
            }
            ++i;
            continue;
        }

        // Short flag: -v or -t value
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
            std::string ch(1, arg[1]);

            // Special: -h always recognized
            if (ch == "h") {
                args.set_flag("help");
                ++i;
                continue;
            }

            // Find matching flag by short name
            const Flag* match = nullptr;
            for (auto& f : flags) {
                if (f.short_name == ch) { match = &f; break; }
            }
            if (!match) {
                return LoomError(LoomError::InvalidArg,
                    "unknown flag '-" + ch + "'");
            }

            if (match->takes_value) {
                // Value can be rest of this arg or next arg
                if (arg.size() > 2) {
                    std::string val = arg.substr(2);
                    if (match->repeatable) {
                        args.add_value(match->long_name, val);
                    } else {
                        args.set_value(match->long_name, val);
                    }
                } else {
                    ++i;
                    if (i >= argc) {
                        return LoomError(LoomError::InvalidArg,
                            "flag '-" + ch + "' requires a value");
                    }
                    if (match->repeatable) {
                        args.add_value(match->long_name, argv[i]);
                    } else {
                        args.set_value(match->long_name, argv[i]);
                    }
                }
            } else {
                // Boolean short flags can be stacked: -vvv
                for (size_t k = 1; k < arg.size(); ++k) {
                    std::string sc(1, arg[k]);
                    const Flag* fm = nullptr;
                    for (auto& f : flags) {
                        if (f.short_name == sc) { fm = &f; break; }
                    }
                    if (!fm) {
                        return LoomError(LoomError::InvalidArg,
                            "unknown flag '-" + sc + "'");
                    }
                    if (fm->takes_value) {
                        return LoomError(LoomError::InvalidArg,
                            "flag '-" + sc + "' requires a value and cannot be stacked");
                    }
                    args.set_flag(fm->long_name);
                }
            }
            ++i;
            continue;
        }

        // Single "-" is treated as positional
        args.add_positional(arg);
        ++i;
    }
    return Result<int>::ok(i);
}

Command* CliParser::find_command(const std::string& name) {
    for (auto& c : commands_) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

std::string CliParser::suggest_command(const std::string& name) const {
    std::string best;
    size_t best_dist = std::string::npos;
    for (auto& c : commands_) {
        size_t d = levenshtein(name, c.name);
        if (d < best_dist) {
            best_dist = d;
            best = c.name;
        }
    }
    // Threshold: distance <= 2 or distance <= name.size()/2
    if (best_dist <= 2 || (name.size() > 0 && best_dist <= name.size() / 2)) {
        return best;
    }
    return "";
}

} // namespace loom
