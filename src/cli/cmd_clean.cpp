#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/cache.hpp>
#include <loom/build_cache.hpp>
#include <iostream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_clean(CliArgs& /*global*/, CliArgs& cmd) {
    auto cwd = fs::current_path();

    // Discover project to find .loom/ directory
    auto proj_r = Project::discover(cwd);
    if (proj_r.is_err()) return std::move(proj_r).error();
    auto& project = proj_r.value();

    auto loom_dir = project.root_dir / ".loom";

    if (cmd.has("all")) {
        // Remove entire .loom/ directory
        if (!fs::exists(loom_dir)) {
            std::cout << "Nothing to clean (no .loom/ directory)\n";
            return Result<int>::ok(0);
        }

        std::error_code ec;
        auto removed = fs::remove_all(loom_dir, ec);
        if (ec) {
            return LoomError(LoomError::IO,
                "failed to remove .loom/ directory: " + ec.message());
        }

        std::cout << "Removed .loom/ directory ("
                  << removed << " entries)\n";

        return Result<int>::ok(0);
    }

    // Default: remove build cache db + clean checkouts
    bool cleaned_anything = false;

    // Remove the build cache database
    auto db_path = loom_dir / "cache" / "loom_cache.db";
    if (fs::exists(db_path)) {
        std::error_code ec;
        fs::remove(db_path, ec);
        if (ec) {
            return LoomError(LoomError::IO,
                "failed to remove build cache: " + ec.message());
        }
        std::cout << "Removed build cache: " << db_path.string() << "\n";
        cleaned_anything = true;
    }

    // Also remove WAL and SHM files if present (SQLite journal files)
    for (const auto& suffix : {"-wal", "-shm"}) {
        auto journal_path = db_path.string() + suffix;
        if (fs::exists(journal_path)) {
            std::error_code ec;
            fs::remove(journal_path, ec);
            if (ec) {
                log::warn("failed to remove %s: %s",
                          journal_path.c_str(), ec.message().c_str());
            }
        }
    }

    // Clean git checkouts via CacheManager
    CacheManager cache(CacheManager::default_cache_root());
    auto clean_r = cache.clean_checkouts();
    if (clean_r.is_err()) {
        log::warn("failed to clean checkouts: %s",
                  clean_r.error().message.c_str());
    } else {
        std::cout << "Cleaned git checkouts\n";
        cleaned_anything = true;
    }

    if (!cleaned_anything) {
        std::cout << "Nothing to clean\n";
    }

    return Result<int>::ok(0);
}

void register_clean(CliParser& cli) {
    Command cmd;
    cmd.name = "clean";
    cmd.summary = "Clean build artifacts and caches";
    cmd.description = "Without flags, removes the build cache database (.loom/cache/loom_cache.db) "
                      "and git checkout working trees. With --all, removes the entire .loom/ "
                      "directory including bare git repos and all cached data.";
    cmd.usage = "loom clean [--all]";
    cmd.group = "Dependencies";
    cmd.flags = {
        Flag{
            "all", "a",
            "Remove entire .loom/ directory instead of just caches",
            false, "", "", false
        },
    };
    cmd.handler = handle_clean;
    cli.add_command(std::move(cmd));
}

} // namespace loom
