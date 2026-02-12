#include <catch2/catch.hpp>
#include <loom/cli.hpp>

using namespace loom;

// Helper to create argv from strings
struct ArgHelper {
    std::vector<std::string> storage;
    std::vector<char*> ptrs;

    ArgHelper(std::initializer_list<std::string> args) {
        storage.assign(args);
        for (auto& s : storage) ptrs.push_back(const_cast<char*>(s.c_str()));
    }

    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};

// ---- Levenshtein tests ----

TEST_CASE("levenshtein: identical strings", "[cli]") {
    REQUIRE(levenshtein("build", "build") == 0);
}

TEST_CASE("levenshtein: one edit", "[cli]") {
    REQUIRE(levenshtein("build", "buld") == 1);
    REQUIRE(levenshtein("build", "biuld") == 2);  // transposition = 2 in Levenshtein
}

TEST_CASE("levenshtein: kitten/sitting", "[cli]") {
    REQUIRE(levenshtein("kitten", "sitting") == 3);
}

TEST_CASE("levenshtein: empty strings", "[cli]") {
    REQUIRE(levenshtein("", "") == 0);
    REQUIRE(levenshtein("abc", "") == 3);
    REQUIRE(levenshtein("", "xyz") == 3);
}

// ---- CliArgs tests ----

TEST_CASE("CliArgs: has and get", "[cli]") {
    CliArgs args;
    REQUIRE_FALSE(args.has("verbose"));
    args.set_flag("verbose");
    REQUIRE(args.has("verbose"));
    REQUIRE(args.count("verbose") == 1);
}

TEST_CASE("CliArgs: set_value and get", "[cli]") {
    CliArgs args;
    args.set_value("target", "sim");
    REQUIRE(args.has("target"));
    REQUIRE(args.get("target") == "sim");
}

TEST_CASE("CliArgs: get_all for repeatable", "[cli]") {
    CliArgs args;
    args.add_value("rule", "no-latch");
    args.add_value("rule", "no-tristate");
    auto all = args.get_all("rule");
    REQUIRE(all.size() == 2);
    REQUIRE(all[0] == "no-latch");
    REQUIRE(all[1] == "no-tristate");
}

TEST_CASE("CliArgs: count for repeatable bool", "[cli]") {
    CliArgs args;
    args.increment("verbose");
    args.increment("verbose");
    args.increment("verbose");
    REQUIRE(args.count("verbose") == 3);
}

TEST_CASE("CliArgs: positional args", "[cli]") {
    CliArgs args;
    args.add_positional("file1.sv");
    args.add_positional("file2.sv");
    REQUIRE(args.positional().size() == 2);
    REQUIRE(args.positional()[0] == "file1.sv");
}

TEST_CASE("CliArgs: passthrough args", "[cli]") {
    CliArgs args;
    args.add_passthrough("-Wall");
    args.add_passthrough("-O2");
    REQUIRE(args.passthrough().size() == 2);
    REQUIRE(args.passthrough()[0] == "-Wall");
}

// ---- CliParser tests ----

TEST_CASE("CliParser: --help", "[cli]") {
    CliParser cli("loom", "0.1.0");
    cli.add_global_flag({"help", "h", "Print help"});

    ArgHelper a{"loom", "--help"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 0);
}

TEST_CASE("CliParser: --version", "[cli]") {
    CliParser cli("loom", "0.1.0");
    cli.add_global_flag({"version", "", "Print version"});

    ArgHelper a{"loom", "--version"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 0);
}

TEST_CASE("CliParser: unknown command with suggestion", "[cli]") {
    CliParser cli("loom", "0.1.0");
    Command cmd;
    cmd.name = "build";
    cmd.summary = "Build the project";
    cmd.group = "Build";
    cmd.usage = "loom build";
    cmd.handler = [](CliArgs&, CliArgs&) -> Result<int> { return Result<int>::ok(0); };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "buld"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("buld") != std::string::npos);
    REQUIRE(r.error().message.find("build") != std::string::npos);
}

TEST_CASE("CliParser: subcommand dispatch", "[cli]") {
    CliParser cli("loom", "0.1.0");
    bool called = false;
    Command cmd;
    cmd.name = "test";
    cmd.summary = "Run tests";
    cmd.group = "Build";
    cmd.usage = "loom test";
    cmd.handler = [&](CliArgs&, CliArgs&) -> Result<int> {
        called = true;
        return Result<int>::ok(42);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "test"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 42);
    REQUIRE(called);
}

TEST_CASE("CliParser: global flags before subcommand", "[cli]") {
    CliParser cli("loom", "0.1.0");
    cli.add_global_flag({"verbose", "v", "Verbose", false, "", "", true});

    int verbosity = 0;
    Command cmd;
    cmd.name = "info";
    cmd.summary = "Show info";
    cmd.group = "Project";
    cmd.usage = "loom info";
    cmd.handler = [&](CliArgs& g, CliArgs&) -> Result<int> {
        verbosity = g.count("verbose");
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "-vv", "info"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(verbosity == 2);
}

TEST_CASE("CliParser: command-specific flags", "[cli]") {
    CliParser cli("loom", "0.1.0");

    std::string format_val;
    bool strict = false;
    Command cmd;
    cmd.name = "lint";
    cmd.summary = "Lint files";
    cmd.group = "Quality";
    cmd.usage = "loom lint [flags]";
    cmd.flags = {
        {"format", "f", "Output format", true, "FMT", "text"},
        {"strict", "", "Strict mode"}
    };
    cmd.handler = [&](CliArgs&, CliArgs& c) -> Result<int> {
        format_val = c.get("format");
        strict = c.has("strict");
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "lint", "--format", "json", "--strict"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(format_val == "json");
    REQUIRE(strict);
}

TEST_CASE("CliParser: positional args captured", "[cli]") {
    CliParser cli("loom", "0.1.0");

    std::vector<std::string> files;
    Command cmd;
    cmd.name = "lint";
    cmd.summary = "Lint files";
    cmd.group = "Quality";
    cmd.usage = "loom lint [files...]";
    cmd.handler = [&](CliArgs&, CliArgs& c) -> Result<int> {
        files = c.positional();
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "lint", "foo.sv", "bar.sv"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(files.size() == 2);
    REQUIRE(files[0] == "foo.sv");
    REQUIRE(files[1] == "bar.sv");
}

TEST_CASE("CliParser: pass-through after --", "[cli]") {
    CliParser cli("loom", "0.1.0");

    std::vector<std::string> passthrough;
    Command cmd;
    cmd.name = "build";
    cmd.summary = "Build project";
    cmd.group = "Build";
    cmd.usage = "loom build [-- ...]";
    cmd.handler = [&](CliArgs&, CliArgs& c) -> Result<int> {
        passthrough = c.passthrough();
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "build", "--", "-Wall", "-O2"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(passthrough.size() == 2);
    REQUIRE(passthrough[0] == "-Wall");
    REQUIRE(passthrough[1] == "-O2");
}

TEST_CASE("CliParser: repeatable flags", "[cli]") {
    CliParser cli("loom", "0.1.0");

    std::vector<std::string> rules;
    Command cmd;
    cmd.name = "lint";
    cmd.summary = "Lint";
    cmd.group = "Quality";
    cmd.usage = "loom lint";
    cmd.flags = {
        {"rule", "r", "Filter rule", true, "ID", "", true}
    };
    cmd.handler = [&](CliArgs&, CliArgs& c) -> Result<int> {
        rules = c.get_all("rule");
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "lint", "--rule", "no-latch", "--rule", "no-tristate"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(rules.size() == 2);
    REQUIRE(rules[0] == "no-latch");
    REQUIRE(rules[1] == "no-tristate");
}

TEST_CASE("CliParser: unknown flag error", "[cli]") {
    CliParser cli("loom", "0.1.0");

    Command cmd;
    cmd.name = "info";
    cmd.summary = "Show info";
    cmd.group = "Project";
    cmd.usage = "loom info";
    cmd.handler = [](CliArgs&, CliArgs&) -> Result<int> { return Result<int>::ok(0); };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "info", "--nonexistent"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("unknown flag") != std::string::npos);
}

TEST_CASE("CliParser: missing flag value error", "[cli]") {
    CliParser cli("loom", "0.1.0");

    Command cmd;
    cmd.name = "lint";
    cmd.summary = "Lint";
    cmd.group = "Quality";
    cmd.usage = "loom lint";
    cmd.flags = {
        {"format", "f", "Format", true, "FMT"}
    };
    cmd.handler = [](CliArgs&, CliArgs&) -> Result<int> { return Result<int>::ok(0); };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "lint", "--format"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_err());
    REQUIRE(r.error().message.find("requires a value") != std::string::npos);
}

TEST_CASE("CliParser: per-command --help", "[cli]") {
    CliParser cli("loom", "0.1.0");

    bool handler_called = false;
    Command cmd;
    cmd.name = "build";
    cmd.summary = "Build the project";
    cmd.description = "Compiles and runs the design";
    cmd.group = "Build";
    cmd.usage = "loom build [flags]";
    cmd.flags = {
        {"wave", "", "Enable waveform dump"}
    };
    cmd.handler = [&](CliArgs&, CliArgs&) -> Result<int> {
        handler_called = true;
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "build", "--help"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 0);
    REQUIRE_FALSE(handler_called);
}

TEST_CASE("CliParser: help text contains grouped commands", "[cli]") {
    CliParser cli("loom", "0.1.0");
    cli.add_global_flag({"help", "h", "Help"});

    Command c1;
    c1.name = "new"; c1.summary = "Create project"; c1.group = "Project";
    c1.usage = "loom new"; c1.handler = [](CliArgs&, CliArgs&) -> Result<int> { return Result<int>::ok(0); };
    cli.add_command(std::move(c1));

    Command c2;
    c2.name = "build"; c2.summary = "Build project"; c2.group = "Build";
    c2.usage = "loom build"; c2.handler = [](CliArgs&, CliArgs&) -> Result<int> { return Result<int>::ok(0); };
    cli.add_command(std::move(c2));

    auto text = cli.help_text();
    REQUIRE(text.find("Project Commands:") != std::string::npos);
    REQUIRE(text.find("Build Commands:") != std::string::npos);
    REQUIRE(text.find("new") != std::string::npos);
    REQUIRE(text.find("build") != std::string::npos);
}

TEST_CASE("CliParser: flag with = syntax", "[cli]") {
    CliParser cli("loom", "0.1.0");

    std::string output;
    Command cmd;
    cmd.name = "plan";
    cmd.summary = "Plan";
    cmd.group = "Build";
    cmd.usage = "loom plan";
    cmd.flags = {
        {"output", "o", "Output file", true, "FILE"}
    };
    cmd.handler = [&](CliArgs&, CliArgs& c) -> Result<int> {
        output = c.get("output");
        return Result<int>::ok(0);
    };
    cli.add_command(std::move(cmd));

    ArgHelper a{"loom", "plan", "--output=filelist.f"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(output == "filelist.f");
}

TEST_CASE("CliParser: no args shows help", "[cli]") {
    CliParser cli("loom", "0.1.0");

    ArgHelper a{"loom"};
    auto r = cli.run(a.argc(), a.argv());
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 0);
}
