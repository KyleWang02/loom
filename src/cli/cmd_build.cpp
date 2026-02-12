#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/workspace.hpp>
#include <loom/lockfile.hpp>
#include <loom/resolver.hpp>
#include <loom/local_override.hpp>
#include <loom/cache.hpp>
#include <loom/filelist.hpp>
#include <loom/tool_driver.hpp>
#include <loom/tool_types.hpp>
#include <loom/target_expr.hpp>
#include <iostream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

// Shared implementation for both "build" and "test" commands.
// The action parameter distinguishes them: test forces ToolAction::Simulate.
static Result<int> run_build(CliArgs& global, CliArgs& cmd, ToolAction action) {
    auto cwd = fs::current_path();

    // --- Discover project ---
    auto project_r = Project::discover(cwd);
    if (project_r.is_err()) {
        return std::move(project_r).error();
    }
    auto project = std::move(project_r).value();

    // --- Resolve workspace members if in a workspace ---
    bool in_workspace = false;
    {
        auto ws_r = is_workspace_root(project.root_dir);
        if (ws_r.is_ok()) {
            in_workspace = ws_r.value();
        }
    }

    // If in a workspace, resolve which members to operate on
    if (in_workspace) {
        auto ws_r = Workspace::discover(cwd);
        if (ws_r.is_err()) {
            return std::move(ws_r).error();
        }
        auto& workspace = ws_r.value();

        auto pkg_flags = cmd.get_all("p");
        bool all_flag = cmd.has("all");

        auto targets_r = workspace.resolve_targets(pkg_flags, all_flag, cwd);
        if (targets_r.is_err()) {
            return std::move(targets_r).error();
        }
        auto& targets = targets_r.value();

        // Run build for each resolved workspace member
        int last_exit = 0;
        for (auto* member : targets) {
            log::info("building member '%s'", member->name.c_str());

            auto member_project_r = Project::load(member->root_dir);
            if (member_project_r.is_err()) {
                return std::move(member_project_r).error();
            }
            auto member_project = std::move(member_project_r).value();

            // Load or create lockfile (workspace uses root lockfile)
            auto lock_path = (workspace.root_dir() / "Loom.lock").string();
            LockFile lockfile;
            if (fs::exists(lock_path)) {
                auto lock_r = LockFile::load(lock_path);
                if (lock_r.is_err()) {
                    return std::move(lock_r).error();
                }
                lockfile = std::move(lock_r).value();
            }

            if (lockfile.packages.empty() ||
                lockfile.is_stale(member_project.manifest.dependencies)) {
                log::info("resolving dependencies...");
                CacheManager cache(CacheManager::default_cache_root());
                DependencyResolver resolver(cache);
                ResolveOptions ropts;
                ropts.offline = global.has("offline");
                auto resolve_r = resolver.resolve_workspace(workspace, lockfile, ropts);
                if (resolve_r.is_err()) {
                    return std::move(resolve_r).error();
                }
                lockfile = std::move(resolve_r).value();
                LOOM_TRY(lockfile.save(lock_path));
            }

            // Apply local overrides unless suppressed
            bool no_local = cmd.has("no-local");
            if (!should_suppress_overrides(no_local)) {
                auto overrides_r = discover_local_overrides(workspace.root_dir());
                if (overrides_r.is_ok()) {
                    auto& overrides = overrides_r.value();
                    if (!overrides.empty()) {
                        overrides.warn_active();
                        LOOM_TRY(DependencyResolver::apply_overrides(lockfile, overrides));
                    }
                }
            }

            // Generate filelist
            FilelistOptions fl_opts;
            if (cmd.has("target")) {
                auto ts_r = parse_target_set(cmd.get("target"));
                if (ts_r.is_err()) {
                    return std::move(ts_r).error();
                }
                fl_opts.active_targets = std::move(ts_r).value();
            }

            FilelistGenerator gen;
            auto filelist_r = gen.generate(member_project, fl_opts);
            if (filelist_r.is_err()) {
                return std::move(filelist_r).error();
            }
            auto filelist = std::move(filelist_r).value();

            // Select driver
            std::unique_ptr<ToolDriver> driver;
            std::string target_name = cmd.get("target");
            TargetConfig target_cfg;

            if (!target_name.empty()) {
                auto it = member_project.manifest.targets.find(target_name);
                if (it == member_project.manifest.targets.end()) {
                    return LoomError(LoomError::NotFound,
                        "target '" + target_name + "' not found in manifest",
                        "available targets are defined in [targets] of Loom.toml");
                }
                target_cfg = it->second;
                driver = create_driver(target_cfg);
            } else {
                auto driver_r = detect_driver(action);
                if (driver_r.is_err()) {
                    return std::move(driver_r).error();
                }
                driver = std::move(driver_r).value();
                target_cfg.name = driver->name();
                target_cfg.action = tool_action_name(action);
            }

            if (!driver) {
                return LoomError(LoomError::NotFound,
                    "no suitable EDA tool driver found",
                    "install a supported tool or specify --target");
            }

            // Build tool options from target config + CLI overrides
            auto tool_opts = ToolOptions::from_map(target_cfg.options);
            if (cmd.has("wave")) {
                tool_opts.waveform = true;
                tool_opts.waveform_format = cmd.has("wave-format")
                    ? cmd.get("wave-format") : "vcd";
            }

            // Forward pass-through args
            auto& passthrough = cmd.passthrough();
            if (!passthrough.empty()) {
                std::string joined;
                for (size_t i = 0; i < passthrough.size(); ++i) {
                    if (i > 0) joined += " ";
                    joined += passthrough[i];
                }
                tool_opts.extra["passthrough"] = joined;
            }

            // Execute
            auto build_root = (member_project.root_dir / ".loom" / "build").string();
            auto result_r = driver->execute(filelist, target_cfg, build_root);
            if (result_r.is_err()) {
                return std::move(result_r).error();
            }
            auto& result = result_r.value();

            // Print output
            if (!result.stdout_log.empty()) {
                std::cout << result.stdout_log;
            }
            if (!result.stderr_log.empty()) {
                std::cerr << result.stderr_log;
            }

            last_exit = result.exit_code;
            if (last_exit != 0) {
                return Result<int>::ok(last_exit);
            }
        }

        return Result<int>::ok(last_exit);
    }

    // --- Single project (non-workspace) path ---

    // Load or create lockfile
    auto lock_path = (project.root_dir / "Loom.lock").string();
    LockFile lockfile;
    if (fs::exists(lock_path)) {
        auto lock_r = LockFile::load(lock_path);
        if (lock_r.is_err()) {
            return std::move(lock_r).error();
        }
        lockfile = std::move(lock_r).value();
    }

    if (lockfile.packages.empty() ||
        lockfile.is_stale(project.manifest.dependencies)) {
        log::info("resolving dependencies...");
        CacheManager cache(CacheManager::default_cache_root());
        DependencyResolver resolver(cache);
        ResolveOptions ropts;
        ropts.offline = global.has("offline");
        auto resolve_r = resolver.resolve(project.manifest, lockfile, ropts);
        if (resolve_r.is_err()) {
            return std::move(resolve_r).error();
        }
        lockfile = std::move(resolve_r).value();
        LOOM_TRY(lockfile.save(lock_path));
    }

    // Apply local overrides unless suppressed
    bool no_local = cmd.has("no-local");
    if (!should_suppress_overrides(no_local)) {
        auto overrides_r = discover_local_overrides(project.root_dir);
        if (overrides_r.is_ok()) {
            auto& overrides = overrides_r.value();
            if (!overrides.empty()) {
                overrides.warn_active();
                LOOM_TRY(DependencyResolver::apply_overrides(lockfile, overrides));
            }
        }
    }

    // Generate filelist
    FilelistOptions fl_opts;
    if (cmd.has("target")) {
        auto ts_r = parse_target_set(cmd.get("target"));
        if (ts_r.is_err()) {
            return std::move(ts_r).error();
        }
        fl_opts.active_targets = std::move(ts_r).value();
    }

    FilelistGenerator gen;
    auto filelist_r = gen.generate(project, fl_opts);
    if (filelist_r.is_err()) {
        return std::move(filelist_r).error();
    }
    auto filelist = std::move(filelist_r).value();

    // Select driver
    std::unique_ptr<ToolDriver> driver;
    std::string target_name = cmd.get("target");
    TargetConfig target_cfg;

    if (!target_name.empty()) {
        auto it = project.manifest.targets.find(target_name);
        if (it == project.manifest.targets.end()) {
            return LoomError(LoomError::NotFound,
                "target '" + target_name + "' not found in manifest",
                "available targets are defined in [targets] of Loom.toml");
        }
        target_cfg = it->second;
        driver = create_driver(target_cfg);
    } else {
        auto driver_r = detect_driver(action);
        if (driver_r.is_err()) {
            return std::move(driver_r).error();
        }
        driver = std::move(driver_r).value();
        target_cfg.name = driver->name();
        target_cfg.action = tool_action_name(action);
    }

    if (!driver) {
        return LoomError(LoomError::NotFound,
            "no suitable EDA tool driver found",
            "install a supported tool or specify --target");
    }

    // Build tool options from target config + CLI overrides
    auto tool_opts = ToolOptions::from_map(target_cfg.options);
    if (cmd.has("wave")) {
        tool_opts.waveform = true;
        tool_opts.waveform_format = cmd.has("wave-format")
            ? cmd.get("wave-format") : "vcd";
    }

    // Forward pass-through args
    auto& passthrough = cmd.passthrough();
    if (!passthrough.empty()) {
        std::string joined;
        for (size_t i = 0; i < passthrough.size(); ++i) {
            if (i > 0) joined += " ";
            joined += passthrough[i];
        }
        tool_opts.extra["passthrough"] = joined;
    }

    // Execute
    auto build_root = (project.root_dir / ".loom" / "build").string();
    auto result_r = driver->execute(filelist, target_cfg, build_root);
    if (result_r.is_err()) {
        return std::move(result_r).error();
    }
    auto& result = result_r.value();

    // Print output
    if (!result.stdout_log.empty()) {
        std::cout << result.stdout_log;
    }
    if (!result.stderr_log.empty()) {
        std::cerr << result.stderr_log;
    }

    return Result<int>::ok(result.exit_code);
}

static Result<int> handle_build(CliArgs& global, CliArgs& cmd) {
    // Determine action from --target config, or default to Build
    ToolAction action = ToolAction::Build;
    std::string target_name = cmd.get("target");
    if (!target_name.empty()) {
        // If a target is specified, try to parse its action from the manifest.
        // The actual action parsing happens inside run_build when the target is
        // looked up. Here we just use Build as the default for auto-detect.
        auto cwd = fs::current_path();
        auto project_r = Project::discover(cwd);
        if (project_r.is_ok()) {
            auto& targets = project_r.value().manifest.targets;
            auto it = targets.find(target_name);
            if (it != targets.end() && !it->second.action.empty()) {
                auto action_r = parse_tool_action(it->second.action);
                if (action_r.is_ok()) {
                    action = action_r.value();
                }
            }
        }
    }
    return run_build(global, cmd, action);
}

static Result<int> handle_test(CliArgs& global, CliArgs& cmd) {
    return run_build(global, cmd, ToolAction::Simulate);
}

void register_build(CliParser& cli) {
    // --- build command ---
    {
        Command cmd;
        cmd.name = "build";
        cmd.summary = "Build the project using an EDA tool";
        cmd.description =
            "Resolves dependencies, generates a filelist, and invokes the "
            "configured EDA tool to build or synthesize the design. "
            "Pass-through arguments after '--' are forwarded to the EDA tool.";
        cmd.usage = "loom build [flags] [-- <tool-args>...]";
        cmd.group = "Build";
        cmd.flags = {
            {"target", "t", "Named target from [targets] in Loom.toml",
             true, "NAME", "", false},
            {"p", "p", "Workspace member to build (repeatable)",
             true, "PKG", "", true},
            {"all", "", "Build all workspace members",
             false, "", "", false},
            {"wave", "", "Enable waveform dumping",
             false, "", "", false},
            {"wave-format", "", "Waveform format (vcd, fst, fsdb)",
             true, "FMT", "vcd", false},
            {"no-local", "", "Suppress Loom.local overrides",
             false, "", "", false},
        };
        cmd.handler = handle_build;
        cli.add_command(std::move(cmd));
    }

    // --- test command ---
    {
        Command cmd;
        cmd.name = "test";
        cmd.summary = "Run simulation tests";
        cmd.description =
            "Shorthand for 'loom build' with action set to simulate. "
            "Resolves dependencies, generates a filelist, and invokes a "
            "simulation tool. Pass-through arguments after '--' are forwarded "
            "to the simulator.";
        cmd.usage = "loom test [flags] [-- <tool-args>...]";
        cmd.group = "Build";
        cmd.flags = {
            {"target", "t", "Named target from [targets] in Loom.toml",
             true, "NAME", "", false},
            {"p", "p", "Workspace member to test (repeatable)",
             true, "PKG", "", true},
            {"all", "", "Test all workspace members",
             false, "", "", false},
            {"wave", "", "Enable waveform dumping",
             false, "", "", false},
            {"wave-format", "", "Waveform format (vcd, fst, fsdb)",
             true, "FMT", "vcd", false},
            {"no-local", "", "Suppress Loom.local overrides",
             false, "", "", false},
        };
        cmd.handler = handle_test;
        cli.add_command(std::move(cmd));
    }
}

} // namespace loom
