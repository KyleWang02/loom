#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/name.hpp>
#include <loom/git.hpp>
#include <loom/project.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_new(CliArgs& /*global*/, CliArgs& cmd) {
    auto& pos = cmd.positional();
    if (pos.empty()) {
        return LoomError(LoomError::InvalidArg,
            "missing required argument <name>",
            "usage: loom new <name>");
    }

    const std::string& raw_name = pos[0];

    // Validate the package name
    auto name_r = PkgName::parse(raw_name);
    if (name_r.is_err()) {
        return LoomError(LoomError::InvalidArg,
            "invalid package name '" + raw_name + "': " + name_r.error().message,
            "package names must match [a-zA-Z][a-zA-Z0-9_-]*");
    }
    auto pkg_name = std::move(name_r).value();

    // Determine project directory
    fs::path project_dir = fs::current_path() / pkg_name.raw();

    if (fs::exists(project_dir)) {
        return LoomError(LoomError::IO,
            "directory '" + project_dir.string() + "' already exists",
            "choose a different name or remove the existing directory");
    }

    // Create directory structure
    std::error_code ec;
    fs::create_directories(project_dir / "src", ec);
    if (ec) {
        return LoomError(LoomError::IO,
            "failed to create directory: " + ec.message());
    }

    // Write Loom.toml
    {
        std::string toml =
            "[package]\n"
            "name = \"" + pkg_name.raw() + "\"\n"
            "version = \"0.1.0\"\n"
            "\n"
            "[[sources]]\n"
            "files = [\"src/*.sv\"]\n";

        std::ofstream ofs(project_dir / "Loom.toml");
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to write Loom.toml");
        }
        ofs << toml;
    }

    // Write src/top.sv
    {
        std::string sv =
            "// " + pkg_name.raw() + " - top-level module\n"
            "module top (\n"
            "    input  logic clk,\n"
            "    input  logic rst_n\n"
            ");\n"
            "\n"
            "    // TODO: add your design here\n"
            "\n"
            "endmodule\n";

        std::ofstream ofs(project_dir / "src" / "top.sv");
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to write src/top.sv");
        }
        ofs << sv;
    }

    // Write .gitignore
    {
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

        std::ofstream ofs(project_dir / ".gitignore");
        if (!ofs) {
            return LoomError(LoomError::IO,
                "failed to write .gitignore");
        }
        ofs << gitignore;
    }

    // Run git init
    auto git_r = run_command({"git", "init"}, project_dir.string());
    if (git_r.is_err()) {
        log::warn("failed to initialize git repository: %s",
                  git_r.error().message.c_str());
    } else if (git_r.value().exit_code != 0) {
        log::warn("git init returned non-zero exit code");
    }

    log::info("created package '%s' at %s",
              pkg_name.raw().c_str(),
              project_dir.string().c_str());

    std::cout << "Created new loom package '" << pkg_name.raw()
              << "' at " << project_dir.string() << "\n";

    return Result<int>::ok(0);
}

void register_new(CliParser& cli) {
    Command cmd;
    cmd.name = "new";
    cmd.summary = "Create a new loom package";
    cmd.description = "Creates a new directory with a Loom.toml manifest, "
                      "a starter SystemVerilog source file, and initializes a git repository.";
    cmd.usage = "loom new <name>";
    cmd.group = "Project";
    cmd.flags = {};
    cmd.handler = handle_new;
    cli.add_command(std::move(cmd));
}

} // namespace loom
