#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <loom/project.hpp>
#include <loom/doc.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <tuple>
#include <cstdlib>

namespace loom {

namespace fs = std::filesystem;

static Result<int> handle_doc(CliArgs& /*global*/, CliArgs& cmd) {
    // Discover project
    auto proj_r = Project::discover(fs::current_path());
    if (proj_r.is_err()) return std::move(proj_r).error();
    auto& project = proj_r.value();

    // Collect source files
    auto sources_r = project.collect_sources(TargetSet{});
    if (sources_r.is_err()) return std::move(sources_r).error();
    auto& files = sources_r.value();

    if (files.empty()) {
        log::info("no source files found");
        return Result<int>::ok(0);
    }

    // Parse flags
    std::string format = cmd.has("format") ? cmd.get("format") : "md";
    std::string output_dir_str = cmd.has("output") ? cmd.get("output") : "docs/api";
    bool clean = cmd.has("clean");
    bool open_after = cmd.has("open");

    // Validate format
    if (format != "md" && format != "html") {
        return LoomError(LoomError::InvalidArg,
            "unsupported format '" + format + "'",
            "supported formats: 'md' (Markdown), 'html' (HTML)");
    }

    // Resolve output dir relative to project root
    fs::path output_dir = output_dir_str;
    if (output_dir.is_relative()) {
        output_dir = project.root_dir / output_dir;
    }

    // Lex and parse each source file
    log::info("processing %zu source file(s)...", files.size());

    std::vector<std::tuple<std::string, LexResult, ParseResult>> parsed_files;
    parsed_files.reserve(files.size());

    for (auto& path : files) {
        std::ifstream ifs(path);
        if (!ifs) {
            log::warn("cannot read file: %s (skipping)", path.c_str());
            continue;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());

        // Determine if SystemVerilog based on extension
        bool is_sv = false;
        auto ext = fs::path(path).extension().string();
        if (ext == ".sv") {
            is_sv = true;
        }

        auto lex_r = loom::lex(content, path, is_sv);
        if (lex_r.is_err()) {
            log::warn("failed to lex %s: %s (skipping)",
                      path.c_str(), lex_r.error().message.c_str());
            continue;
        }
        auto lex_result = std::move(lex_r).value();

        auto parse_r = loom::parse(lex_result, path, is_sv);
        if (parse_r.is_err()) {
            log::warn("failed to parse %s: %s (skipping)",
                      path.c_str(), parse_r.error().message.c_str());
            continue;
        }
        auto parse_result = std::move(parse_r).value();

        parsed_files.emplace_back(path,
                                   std::move(lex_result),
                                   std::move(parse_result));
    }

    if (parsed_files.empty()) {
        log::warn("no files could be parsed");
        return Result<int>::ok(0);
    }

    // Build DocModel
    doc::DocExtractor extractor(doc::DocExtractorConfig{});
    auto model = extractor.extract_all(parsed_files, project.manifest.package);
    model.resolve_cross_refs();

    log::info("extracted documentation for %zu design unit(s)",
              model.units.size());

    // Configure renderer
    doc::RenderConfig render_config;
    render_config.output_dir = output_dir;
    render_config.generate_diagrams = true;
    render_config.clean_before_generate = clean;

    // Render
    if (format == "html") {
        doc::HtmlRenderer renderer(render_config);
        auto render_r = renderer.render(model);
        if (render_r.is_err()) return std::move(render_r).error();
    } else {
        doc::MarkdownRenderer renderer(render_config);
        auto render_r = renderer.render(model);
        if (render_r.is_err()) return std::move(render_r).error();
    }

    std::cout << "Generated documentation for " << model.units.size()
              << " design unit(s) in " << output_dir.string() << "\n";

    // Open in browser if requested
    if (open_after) {
        auto index_path = output_dir / (format == "html" ? "index.html" : "index.md");
        std::string open_cmd = "xdg-open " + index_path.string();
        log::info("opening %s", index_path.string().c_str());
        int rc = std::system(open_cmd.c_str());
        if (rc != 0) {
            log::warn("failed to open documentation (exit code %d)", rc);
        }
    }

    return Result<int>::ok(0);
}

void register_doc(CliParser& cli) {
    Command cmd;
    cmd.name = "doc";
    cmd.summary = "Generate project documentation";
    cmd.description = "Extracts documentation from /// comments in Verilog/SystemVerilog "
                      "source files and generates Markdown documentation with module "
                      "descriptions, port tables, parameter tables, and dependency diagrams.";
    cmd.usage = "loom doc [flags]";
    cmd.group = "Quality";
    cmd.flags = {
        Flag{
            "format", "",
            "Output format: 'md' or 'html' (default: 'md')",
            true, "FORMAT", "md", false
        },
        Flag{
            "output", "o",
            "Output directory (default: 'docs/api')",
            true, "DIR", "docs/api", false
        },
        Flag{
            "clean", "",
            "Clean output directory before generating",
            false, "", "", false
        },
        Flag{
            "open", "",
            "Open documentation in browser after generating",
            false, "", "", false
        },
    };
    cmd.handler = handle_doc;
    cli.add_command(std::move(cmd));
}

} // namespace loom
