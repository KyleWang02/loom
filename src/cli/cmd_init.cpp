#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/name.hpp>
#include <loom/project.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_init(CliArgs& /*global*/, CliArgs& cmd) {
    fs::path cwd = fs::current_path();

    // Check that no Loom.toml already exists
    if (has_manifest(cwd)) {
        return LoomError(LoomError::Duplicate,
            "Loom.toml already exists in " + cwd.string(),
            "use 'loom new <name>' to create a new package in a subdirectory");
    }

    bool workspace = cmd.has("workspace");

    if (workspace) {
        // Create workspace Loom.toml
        std::string toml =
            "[workspace]\n"
            "members = [\"ip/*\"]\n";

        std::ofstream ofs(cwd / "Loom.toml");
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to write Loom.toml");
        }
        ofs << toml;

        // Create ip/ directory for workspace members
        std::error_code ec;
        fs::create_directories(cwd / "ip", ec);
        if (ec) {
            log::warn("failed to create ip/ directory: %s", ec.message().c_str());
        }

        log::info("initialized workspace in %s", cwd.string().c_str());
        std::cout << "Initialized loom workspace in " << cwd.string() << "\n";
    } else {
        // Derive package name from directory name
        std::string dir_name = cwd.filename().string();

        auto name_r = PkgName::parse(dir_name);
        if (name_r.is_err()) {
            return LoomError(LoomError::InvalidArg,
                "directory name '" + dir_name + "' is not a valid package name: "
                + name_r.error().message,
                "rename the directory or use 'loom new <name>' instead");
        }
        auto pkg_name = std::move(name_r).value();

        // Create Loom.toml
        std::string toml =
            "[package]\n"
            "name = \"" + pkg_name.raw() + "\"\n"
            "version = \"0.1.0\"\n"
            "\n"
            "[[sources]]\n"
            "files = [\"src/*.sv\"]\n";

        std::ofstream ofs(cwd / "Loom.toml");
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to write Loom.toml");
        }
        ofs << toml;

        // Create src/ directory if it does not exist
        std::error_code ec;
        fs::create_directories(cwd / "src", ec);
        if (ec) {
            log::warn("failed to create src/ directory: %s", ec.message().c_str());
        }

        log::info("initialized package '%s' in %s",
                  pkg_name.raw().c_str(), cwd.string().c_str());
        std::cout << "Initialized loom package '" << pkg_name.raw()
                  << "' in " << cwd.string() << "\n";
    }

    // Write .gitignore if it does not exist
    fs::path gitignore_path = cwd / ".gitignore";
    if (!fs::exists(gitignore_path)) {
        std::string gitignore =
            "# Loom build artifacts\n"
            ".loom/\n"
            "Loom.local\n"
            "\n"
            "# EDA tool outputs\n"
            "*.vcd\n"
            "*.wlf\n"
            "*.log\n"
            "work/\n";

        std::ofstream ofs(gitignore_path);
        if (!ofs) {
            log::warn("failed to write .gitignore");
        } else {
            ofs << gitignore;
        }
    }

    return Result<int>::ok(0);
}

void register_init(CliParser& cli) {
    Command cmd;
    cmd.name = "init";
    cmd.summary = "Initialize a loom package in the current directory";
    cmd.description = "Creates a Loom.toml manifest in the current directory. "
                      "Use --workspace to initialize a workspace root instead of a package.";
    cmd.usage = "loom init [--workspace]";
    cmd.group = "Project";
    cmd.flags = {
        Flag{
            "workspace", "w",
            "Initialize as a workspace instead of a package",
            false, "", "", false
        },
    };
    cmd.handler = handle_init;
    cli.add_command(std::move(cmd));
}

} // namespace loom
