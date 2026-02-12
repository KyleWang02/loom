#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/workspace.hpp>
#include <loom/local_override.hpp>
#include <iostream>
#include <filesystem>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_info(CliArgs& /*global*/, CliArgs& /*cmd*/) {
    fs::path cwd = fs::current_path();

    // Try workspace first, then fall back to project
    auto ws_r = Workspace::discover(cwd);
    if (ws_r.is_ok()) {
        auto& ws = ws_r.value();
        const auto& manifest = ws.root_manifest();

        std::cout << "Workspace Information\n";
        std::cout << "---------------------\n";

        if (!manifest.package.name.empty()) {
            std::cout << "  Name:        " << manifest.package.name << "\n";
        }
        if (!manifest.package.version.empty()) {
            std::cout << "  Version:     " << manifest.package.version << "\n";
        }

        std::cout << "  Root:        " << ws.root_dir().string() << "\n";
        std::cout << "  Members:     " << ws.member_count() << "\n";

        // List members
        for (auto& member : ws.members()) {
            std::cout << "    - " << member.name;
            if (!member.version.empty()) {
                std::cout << " (" << member.version << ")";
            }
            std::cout << " at " << member.root_dir.string() << "\n";
        }

        // Show dependencies from root manifest
        if (!manifest.dependencies.empty()) {
            std::cout << "  Dependencies: " << manifest.dependencies.size() << "\n";
            for (auto& dep : manifest.dependencies) {
                std::cout << "    - " << dep.name;
                if (dep.git.has_value()) {
                    std::cout << " (git: " << dep.git->url;
                    if (dep.git->version.has_value()) {
                        std::cout << ", version: " << dep.git->version.value();
                    }
                    std::cout << ")";
                } else if (dep.path.has_value()) {
                    std::cout << " (path: " << dep.path->path << ")";
                }
                std::cout << "\n";
            }
        } else {
            std::cout << "  Dependencies: 0\n";
        }

        // Show configured targets
        if (!manifest.targets.empty()) {
            std::cout << "  Targets:\n";
            for (auto& [tname, tcfg] : manifest.targets) {
                std::cout << "    - " << tname;
                if (!tcfg.tool.empty()) {
                    std::cout << " (tool: " << tcfg.tool << ")";
                }
                std::cout << "\n";
            }
        }

        // Check for local overrides
        auto ov_r = discover_local_overrides(ws.root_dir());
        if (ov_r.is_ok() && !ov_r.value().empty()) {
            auto& overrides = ov_r.value();
            std::cout << "  Local Overrides: " << overrides.count() << " active\n";
            for (auto& [name, src] : overrides.overrides) {
                std::cout << "    - " << name;
                if (src.kind == OverrideSource::Kind::Path) {
                    std::cout << " -> path: " << src.path;
                } else {
                    std::cout << " -> git: " << src.url;
                }
                std::cout << "\n";
            }
        }

        return Result<int>::ok(0);
    }

    // Fall back to single project
    auto proj_r = Project::discover(cwd);
    if (proj_r.is_err()) {
        return LoomError(LoomError::NotFound,
            "no Loom.toml found in " + cwd.string() + " or any parent directory",
            "run 'loom init' to create a new project");
    }
    auto& proj = proj_r.value();
    auto& manifest = proj.manifest;

    std::cout << "Package Information\n";
    std::cout << "-------------------\n";
    std::cout << "  Name:        " << manifest.package.name << "\n";
    std::cout << "  Version:     " << manifest.package.version << "\n";
    std::cout << "  Root:        " << proj.root_dir.string() << "\n";

    if (!manifest.package.top.empty()) {
        std::cout << "  Top Module:  " << manifest.package.top << "\n";
    }

    if (!manifest.package.authors.empty()) {
        std::cout << "  Authors:\n";
        for (auto& author : manifest.package.authors) {
            std::cout << "    - " << author << "\n";
        }
    }

    // Dependencies
    if (!manifest.dependencies.empty()) {
        std::cout << "  Dependencies: " << manifest.dependencies.size() << "\n";
        for (auto& dep : manifest.dependencies) {
            std::cout << "    - " << dep.name;
            if (dep.git.has_value()) {
                std::cout << " (git: " << dep.git->url;
                if (dep.git->version.has_value()) {
                    std::cout << ", version: " << dep.git->version.value();
                }
                std::cout << ")";
            } else if (dep.path.has_value()) {
                std::cout << " (path: " << dep.path->path << ")";
            } else if (dep.workspace) {
                std::cout << " (workspace)";
            } else if (dep.member) {
                std::cout << " (member)";
            }
            std::cout << "\n";
        }
    } else {
        std::cout << "  Dependencies: 0\n";
    }

    // Show configured targets
    if (!manifest.targets.empty()) {
        std::cout << "  Targets:\n";
        for (auto& [tname, tcfg] : manifest.targets) {
            std::cout << "    - " << tname;
            if (!tcfg.tool.empty()) {
                std::cout << " (tool: " << tcfg.tool << ")";
            }
            std::cout << "\n";
        }
    }

    // Check for local overrides
    auto ov_r = discover_local_overrides(proj.root_dir);
    if (ov_r.is_ok() && !ov_r.value().empty()) {
        auto& overrides = ov_r.value();
        std::cout << "  Local Overrides: " << overrides.count() << " active\n";
        for (auto& [name, src] : overrides.overrides) {
            std::cout << "    - " << name;
            if (src.kind == OverrideSource::Kind::Path) {
                std::cout << " -> path: " << src.path;
            } else {
                std::cout << " -> git: " << src.url;
            }
            std::cout << "\n";
        }
    }

    return Result<int>::ok(0);
}

void register_info(CliParser& cli) {
    Command cmd;
    cmd.name = "info";
    cmd.summary = "Show project or workspace information";
    cmd.description = "Displays metadata about the current loom package or workspace, "
                      "including dependencies, targets, and active local overrides.";
    cmd.usage = "loom info";
    cmd.group = "Project";
    cmd.flags = {};
    cmd.handler = handle_info;
    cli.add_command(std::move(cmd));
}

} // namespace loom
