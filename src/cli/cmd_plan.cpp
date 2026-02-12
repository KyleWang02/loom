#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/workspace.hpp>
#include <loom/lockfile.hpp>
#include <loom/resolver.hpp>
#include <loom/local_override.hpp>
#include <loom/cache.hpp>
#include <loom/filelist.hpp>
#include <loom/target_expr.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_plan(CliArgs& global, CliArgs& cmd) {
    auto cwd = fs::current_path();

    // --- Discover project ---
    auto project_r = Project::discover(cwd);
    if (project_r.is_err()) {
        return std::move(project_r).error();
    }
    auto project = std::move(project_r).value();

    // --- Determine lockfile root (workspace or single project) ---
    fs::path lock_root = project.root_dir;
    bool in_workspace = false;
    {
        auto ws_r = is_workspace_root(project.root_dir);
        if (ws_r.is_ok()) {
            in_workspace = ws_r.value();
        }
    }

    if (!in_workspace) {
        // Check if there is a workspace above this project
        auto ws_r = Workspace::discover(cwd);
        if (ws_r.is_ok()) {
            lock_root = ws_r.value().root_dir();
            in_workspace = true;
        }
    }

    // --- Load or create lockfile ---
    auto lock_path = (lock_root / "Loom.lock").string();
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

        if (in_workspace) {
            auto ws_r = Workspace::discover(cwd);
            if (ws_r.is_err()) {
                return std::move(ws_r).error();
            }
            auto resolve_r = resolver.resolve_workspace(ws_r.value(), lockfile, ropts);
            if (resolve_r.is_err()) {
                return std::move(resolve_r).error();
            }
            lockfile = std::move(resolve_r).value();
        } else {
            auto resolve_r = resolver.resolve(project.manifest, lockfile, ropts);
            if (resolve_r.is_err()) {
                return std::move(resolve_r).error();
            }
            lockfile = std::move(resolve_r).value();
        }
        LOOM_TRY(lockfile.save(lock_path));
    }

    // --- Apply local overrides unless suppressed ---
    bool no_local = cmd.has("no-local");
    if (!should_suppress_overrides(no_local)) {
        auto overrides_r = discover_local_overrides(lock_root);
        if (overrides_r.is_ok()) {
            auto& overrides = overrides_r.value();
            if (!overrides.empty()) {
                overrides.warn_active();
                LOOM_TRY(DependencyResolver::apply_overrides(lockfile, overrides));
            }
        }
    }

    // --- Build filelist options ---
    FilelistOptions fl_opts;

    // Parse --target for target-expression filtering on source groups
    if (cmd.has("target")) {
        auto ts_r = parse_target_set(cmd.get("target"));
        if (ts_r.is_err()) {
            return std::move(ts_r).error();
        }
        fl_opts.active_targets = std::move(ts_r).value();
    }

    // Determine output format
    std::string fmt_str = cmd.has("format") ? cmd.get("format") : "dotf";
    if (fmt_str == "json") {
        fl_opts.format = FilelistFormat::Json;
    } else if (fmt_str == "dotf") {
        fl_opts.format = FilelistFormat::DotF;
    } else {
        return LoomError(LoomError::InvalidArg,
            "unknown format '" + fmt_str + "'",
            "valid formats: dotf, json");
    }

    // --- Generate filelist ---
    FilelistGenerator gen;
    auto filelist_r = gen.generate(project, fl_opts);
    if (filelist_r.is_err()) {
        return std::move(filelist_r).error();
    }
    auto filelist = std::move(filelist_r).value();

    // --- Format output ---
    std::string output;
    if (fl_opts.format == FilelistFormat::Json) {
        output = filelist.to_json();
    } else {
        output = filelist.to_dot_f();
    }

    // --- Write to file or stdout ---
    if (cmd.has("o")) {
        std::string out_path = cmd.get("o");
        // Create parent directories if needed
        auto parent = fs::path(out_path).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                return LoomError(LoomError::IO,
                    "failed to create output directory: " + ec.message());
            }
        }

        std::ofstream ofs(out_path);
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to open output file '" + out_path + "'");
        }
        ofs << output;
        ofs.close();
        log::info("filelist written to %s", out_path.c_str());
    } else {
        std::cout << output;
    }

    // Report summary to stderr
    log::info("filelist: %zu files, %zu top module(s), %zu black box(es)",
              filelist.files.size(),
              filelist.top_modules.size(),
              filelist.black_boxes.size());

    return Result<int>::ok(0);
}

void register_plan(CliParser& cli) {
    Command cmd;
    cmd.name = "plan";
    cmd.summary = "Generate a filelist without executing an EDA tool";
    cmd.description =
        "Resolves dependencies and generates a topologically-sorted filelist "
        "for use with external EDA tools. Outputs in .f (dot-f) or JSON format. "
        "This is the same pipeline as 'loom build', but stops before tool execution.";
    cmd.usage = "loom plan [flags]";
    cmd.group = "Build";
    cmd.flags = {
        {"target", "t", "Target expression for source group filtering",
         true, "EXPR", "", false},
        {"o", "o", "Output file path (default: stdout)",
         true, "FILE", "", false},
        {"format", "", "Output format: dotf or json",
         true, "FMT", "dotf", false},
        {"no-local", "", "Suppress Loom.local overrides",
         false, "", "", false},
    };
    cmd.handler = handle_plan;
    cli.add_command(std::move(cmd));
}

} // namespace loom
