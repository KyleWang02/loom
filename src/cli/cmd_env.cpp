#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/git.hpp>
#include <loom/cache.hpp>
#include <loom/config.hpp>
#include <loom/tool_driver.hpp>
#include <iostream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static constexpr const char* LOOM_VERSION_STR = "0.1.0-dev";

static Result<int> handle_env(CliArgs& /*global*/, CliArgs& cmd) {
    bool show_tools = cmd.has("tools");

    // Loom version
    std::cout << "Loom Environment\n";
    std::cout << "-----------------\n";
    std::cout << "  Version:     " << LOOM_VERSION_STR << "\n";

    // Cache directory
    std::string cache_root = CacheManager::default_cache_root();
    std::cout << "  Cache Dir:   " << cache_root << "\n";

    // Config paths
    std::string global_cfg = global_config_path();
    std::cout << "  Global Config: " << global_cfg;
    if (fs::exists(global_cfg)) {
        std::cout << " (exists)";
    } else {
        std::cout << " (not found)";
    }
    std::cout << "\n";

    // Current directory
    std::cout << "  Working Dir: " << fs::current_path().string() << "\n";

    // Git version
    GitCli git;
    auto git_ver = git.check_version();
    if (git_ver.is_ok()) {
        std::cout << "  Git:         " << git_ver.value() << "\n";
    } else {
        std::cout << "  Git:         not found\n";
        log::warn("git not available: %s", git_ver.error().message.c_str());
    }

    // Probe EDA tools if requested
    if (show_tools) {
        std::cout << "\nEDA Tools\n";
        std::cout << "---------\n";

        auto drivers = available_drivers();
        for (auto& driver_name : drivers) {
            auto driver = create_driver(driver_name);
            if (!driver) continue;

            std::cout << "  " << driver->display_name() << ": ";

            auto exe_r = driver->find_executable();
            if (exe_r.is_ok()) {
                std::cout << exe_r.value();

                // Try to detect version
                auto ver_r = driver->detect_version();
                if (ver_r.is_ok() && !ver_r.value().empty()) {
                    std::cout << " (" << ver_r.value() << ")";
                }
                std::cout << "\n";
            } else {
                std::cout << "not found\n";
            }
        }
    }

    return Result<int>::ok(0);
}

void register_env(CliParser& cli) {
    Command cmd;
    cmd.name = "env";
    cmd.summary = "Show loom environment and tool information";
    cmd.description = "Displays the loom version, cache directory, configuration paths, "
                      "and git version. Use --tools to probe PATH for installed EDA tools.";
    cmd.usage = "loom env [--tools]";
    cmd.group = "Project";
    cmd.flags = {
        Flag{
            "tools", "t",
            "Probe PATH for installed EDA tools",
            false, "", "", false
        },
    };
    cmd.handler = handle_env;
    cli.add_command(std::move(cmd));
}

} // namespace loom
