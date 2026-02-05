// demo_errors.cpp
//
// A small standalone program that exercises the LoomError, Result<T>, and
// logging systems against a real Verilog file on disk.  Run it with:
//
//     ./demo_errors                          # no args  -> triggers a "missing arg" error
//     ./demo_errors ../tests/fixtures/simple_module.v   # good file -> happy path
//     ./demo_errors nonexistent.v            # bad path -> IO error
//
// Watch stderr for colored log output and formatted error messages.

#include <loom/result.hpp>
#include <loom/log.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace loom;

// ---------------------------------------------------------------------------
// Simulated "operations" that return Result<T> so we can show error handling
// ---------------------------------------------------------------------------

// Validate that the user gave us a file path on the command line.
Result<std::string> parse_args(int argc, char** argv) {
    if (argc < 2) {
        // No file argument -- produce a clear error with a hint.
        return LoomError{
            LoomError::InvalidArg,
            "no input file specified",
            "usage: demo_errors <file.v>"
        };
    }
    return Result<std::string>::ok(argv[1]);
}

// Check that the path exists and has a Verilog extension.
Result<fs::path> validate_path(const std::string& raw) {
    fs::path p(raw);

    if (!fs::exists(p)) {
        return LoomError{
            LoomError::IO,
            "file does not exist: " + raw,
            "double-check the path and try again"
        };
    }

    std::string ext = p.extension().string();
    if (ext != ".v" && ext != ".sv" && ext != ".vl" && ext != ".vlg") {
        return LoomError{
            LoomError::Parse,
            "unsupported file extension: " + ext,
            "loom only supports .v, .sv, .vl, and .vlg files"
        };
    }

    return Result<fs::path>::ok(p);
}

// Read the entire file into a string.
Result<std::string> read_file(const fs::path& p) {
    std::ifstream in(p);
    if (!in.is_open()) {
        return LoomError{
            LoomError::IO,
            "could not open file: " + p.string(),
            "check file permissions"
        };
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return Result<std::string>::ok(buf.str());
}

// Tiny fake "parser": scan lines for the keyword `module` and return the
// module name that follows it.  Good enough to prove the pipeline works.
Result<std::string> find_module_name(const std::string& source, const std::string& filename) {
    std::istringstream stream(source);
    std::string line;
    int lineno = 0;

    while (std::getline(stream, line)) {
        ++lineno;
        // Look for "module <name>"
        auto pos = line.find("module");
        if (pos == std::string::npos) continue;

        // Skip if "module" is in the middle of another word (e.g. "endmodule")
        if (pos > 0 && std::isalpha(line[pos - 1])) continue;

        // Grab the identifier after "module "
        auto name_start = line.find_first_not_of(" \t", pos + 6);
        if (name_start == std::string::npos) {
            return LoomError{
                LoomError::Parse,
                "found 'module' keyword but no name after it",
                "expected: module <identifier>",
                filename,
                lineno
            };
        }
        auto name_end = line.find_first_of(" \t(#;", name_start);
        std::string name = line.substr(name_start, name_end - name_start);
        return Result<std::string>::ok(name);
    }

    return LoomError{
        LoomError::NotFound,
        "no module declaration found in " + filename,
        "make sure the file contains at least one 'module' keyword"
    };
}

// ---------------------------------------------------------------------------
// A higher-level function that chains everything with LOOM_TRY.
// If any step fails, the error propagates up immediately.
// ---------------------------------------------------------------------------
Result<std::string> process_file(int argc, char** argv) {
    // Each LOOM_TRY line will short-circuit if the call returns an error.
    // On success, the result is stored in the variable on the left.

    auto arg = parse_args(argc, argv);
    LOOM_TRY(arg);                        // propagates InvalidArg

    auto path = validate_path(arg.value());
    LOOM_TRY(path);                       // propagates IO or Parse

    log::info("reading %s", path.value().c_str());

    auto source = read_file(path.value());
    LOOM_TRY(source);                     // propagates IO

    log::info("file is %zu bytes", source.value().size());

    auto name = find_module_name(source.value(), path.value().string());
    LOOM_TRY(name);                       // propagates Parse or NotFound

    return name;
}

// ---------------------------------------------------------------------------
// main -- run the pipeline and print the outcome
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // Turn on verbose logging so we see trace/debug too.
    log::set_level(log::Trace);

    log::trace("demo starting, argc = %d", argc);
    log::debug("log levels: trace through error are all visible");

    auto result = process_file(argc, argv);

    if (result.is_ok()) {
        // Happy path -- print the module name.
        log::info("found module: %s", result.value().c_str());
        std::cout << "module name: " << result.value() << "\n";
        return 0;
    } else {
        // Something went wrong -- print the formatted error.
        log::error("pipeline failed");
        std::cerr << "\n" << result.error().format() << "\n";
        return 1;
    }
}
