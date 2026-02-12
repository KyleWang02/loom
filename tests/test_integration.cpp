#include <catch2/catch.hpp>
#include <loom/project.hpp>
#include <loom/workspace.hpp>
#include <loom/filelist.hpp>
#include <loom/build_cache.hpp>
#include <loom/local_override.hpp>
#include <loom/lockfile.hpp>
#include <loom/sr.hpp>
#include <loom/lint.hpp>
#include <loom/doc.hpp>
#include <loom/util.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>

using namespace loom;
namespace fs = std::filesystem;

// ---- RAII temp directory ----

struct TempDir {
    fs::path path;

    TempDir() {
        auto base = fs::temp_directory_path();
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto h = std::hash<decltype(now)>{}(now);
        path = base / ("loom_integ_" + std::to_string(h));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    std::string write_file(const std::string& rel, const std::string& content) {
        auto full = path / rel;
        fs::create_directories(full.parent_path());
        std::ofstream f(full);
        f << content;
        return full.string();
    }
};

// ========================================================================
// Project lifecycle tests
// ========================================================================

TEST_CASE("integration: project discover and collect sources", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[package]
name = "test_proj"
version = "0.1.0"

[[sources]]
files = ["src/top.sv", "src/sub.sv"]
)TOML");
    td.write_file("src/top.sv",
        "module top(input clk, output out);\n"
        "  sub u_sub(.clk(clk), .out(out));\n"
        "endmodule\n");
    td.write_file("src/sub.sv",
        "module sub(input clk, output out);\n"
        "  assign out = clk;\n"
        "endmodule\n");

    auto proj = Project::load(td.path);
    REQUIRE(proj.is_ok());
    CHECK(proj.value().manifest.package.name == "test_proj");
    CHECK(proj.value().manifest.package.version == "0.1.0");

    auto sources = proj.value().collect_sources({});
    REQUIRE(sources.is_ok());
    CHECK(sources.value().size() == 2);
}

TEST_CASE("integration: project discover walks up directory tree", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[package]
name = "parent_proj"
version = "0.1.0"
)TOML");
    fs::create_directories(td.path / "src" / "deep" / "nested");

    auto proj = Project::discover(td.path / "src" / "deep" / "nested");
    REQUIRE(proj.is_ok());
    CHECK(proj.value().manifest.package.name == "parent_proj");
}

// ========================================================================
// Workspace tests
// ========================================================================

TEST_CASE("integration: workspace discover and list members", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[workspace]
members = ["ip/*"]
)TOML");
    td.write_file("ip/uart/Loom.toml", R"TOML(
[package]
name = "uart"
version = "1.0.0"
)TOML");
    td.write_file("ip/spi/Loom.toml", R"TOML(
[package]
name = "spi"
version = "0.5.0"
)TOML");

    auto ws = Workspace::load(td.path);
    REQUIRE(ws.is_ok());
    CHECK(ws.value().member_count() == 2);
    CHECK(ws.value().find_member("uart") != nullptr);
    CHECK(ws.value().find_member("spi") != nullptr);
}

TEST_CASE("integration: workspace resolve targets", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[workspace]
members = ["ip/*"]
)TOML");
    td.write_file("ip/uart/Loom.toml", R"TOML(
[package]
name = "uart"
version = "1.0.0"
)TOML");
    td.write_file("ip/spi/Loom.toml", R"TOML(
[package]
name = "spi"
version = "0.5.0"
)TOML");

    auto ws = Workspace::load(td.path);
    REQUIRE(ws.is_ok());

    // Resolve by -p flag
    auto targets = ws.value().resolve_targets({"uart"}, false, td.path);
    REQUIRE(targets.is_ok());
    REQUIRE(targets.value().size() == 1);
    CHECK(targets.value()[0]->name == "uart");

    // Resolve all
    auto all = ws.value().resolve_targets({}, true, td.path);
    REQUIRE(all.is_ok());
    CHECK(all.value().size() == 2);
}

// ========================================================================
// Filelist generation pipeline tests
// ========================================================================

TEST_CASE("integration: filelist from project sources", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[package]
name = "filelist_test"
version = "0.1.0"

[[sources]]
files = ["src/top.v", "src/sub.v"]
)TOML");
    td.write_file("src/top.v",
        "module top(input clk);\n"
        "  sub u_sub(.clk(clk));\n"
        "endmodule\n");
    td.write_file("src/sub.v",
        "module sub(input clk);\n"
        "  assign x = clk;\n"
        "endmodule\n");

    auto proj = Project::load(td.path);
    REQUIRE(proj.is_ok());

    FilelistGenerator gen;
    auto result = gen.generate(proj.value());
    REQUIRE(result.is_ok());

    auto& fl = result.value();
    CHECK(fl.files.size() == 2);
    // sub.v should come before top.v (providers-first order)
    CHECK(fl.files[0].file_path.find("sub.v") != std::string::npos);
    CHECK(fl.files[1].file_path.find("top.v") != std::string::npos);
    CHECK(fl.top_modules.size() == 1);
    CHECK(fl.top_modules[0] == "top");
}

TEST_CASE("integration: filelist with target filtering", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[package]
name = "target_test"
version = "0.1.0"

[[sources]]
files = ["src/core.sv"]

[[sources]]
target = "simulation"
files = ["tb/tb_top.sv"]
)TOML");
    td.write_file("src/core.sv",
        "module core(input clk); endmodule\n");
    td.write_file("tb/tb_top.sv",
        "module tb_top;\n"
        "  core u_core(.clk(clk));\n"
        "endmodule\n");

    auto proj = Project::load(td.path);
    REQUIRE(proj.is_ok());

    // Without simulation target: only core
    FilelistOptions opts;
    FilelistGenerator gen;
    auto r1 = gen.generate(proj.value(), opts);
    REQUIRE(r1.is_ok());
    CHECK(r1.value().files.size() == 1);

    // With simulation target: both (include testbenches since tb_top matches heuristic)
    opts.active_targets.insert("simulation");
    opts.include_testbenches = true;
    auto r2 = gen.generate(proj.value(), opts);
    REQUIRE(r2.is_ok());
    CHECK(r2.value().files.size() == 2);
}

// ========================================================================
// Build cache incremental tests
// ========================================================================

TEST_CASE("integration: build cache stat + parse roundtrip", "[integration]") {
    TempDir td;
    auto db_path = (td.path / "cache.db").string();

    BuildCache cache;
    auto st = cache.open(db_path);
    REQUIRE(st.is_ok());

    // Store a stat entry
    FileStatEntry stat_entry;
    stat_entry.path = "/tmp/test.sv";
    stat_entry.inode = 12345;
    stat_entry.mtime_sec = 1000000;
    stat_entry.size = 256;
    stat_entry.content_hash = "abc123";
    REQUIRE(cache.update_stat(stat_entry).is_ok());

    // Retrieve it
    auto lookup = cache.lookup_stat("/tmp/test.sv");
    REQUIRE(lookup.is_ok());
    CHECK(lookup.value().content_hash == "abc123");
    CHECK(lookup.value().inode == 12345);

    // Store a parse result
    ParseResult pr;
    DesignUnit u;
    u.name = "test_mod";
    u.kind = DesignUnitKind::Module;
    u.start_line = 1;
    u.end_line = 10;
    pr.units.push_back(std::move(u));
    REQUIRE(cache.store_parse("abc123", pr).is_ok());

    // Retrieve parse result
    auto pr_lookup = cache.lookup_parse("abc123");
    REQUIRE(pr_lookup.is_ok());
    CHECK(pr_lookup.value().units.size() == 1);
    CHECK(pr_lookup.value().units[0].name == "test_mod");
}

TEST_CASE("integration: build cache filelist cache", "[integration]") {
    TempDir td;
    auto db_path = (td.path / "cache.db").string();

    BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    FilelistCacheEntry entry;
    entry.filelist_key = "key123";
    entry.file_list = {"a.v", "b.v", "c.v"};
    entry.top_modules = {"top"};
    REQUIRE(cache.store_filelist(entry).is_ok());

    auto lookup = cache.lookup_filelist("key123");
    REQUIRE(lookup.is_ok());
    CHECK(lookup.value().file_list.size() == 3);
    CHECK(lookup.value().top_modules[0] == "top");

    // Non-existent key returns error
    auto miss = cache.lookup_filelist("nonexistent");
    CHECK(miss.is_err());
}

// ========================================================================
// Lint engine on real project
// ========================================================================

TEST_CASE("integration: lint engine on project files", "[integration]") {
    TempDir td;
    td.write_file("test.sv", R"(
module lint_test(input clk, input rst);
    logic [7:0] counter;

    always_ff @(posedge clk) begin
        if (rst)
            counter = 8'h0;  // blocking in always_ff!
        else
            counter <= counter + 1;
    end
endmodule
)");

    lint::LintEngine engine;
    auto report = engine.lint_file((td.path / "test.sv").string());
    REQUIRE(report.is_ok());

    bool found_blocking = false;
    for (auto& d : report.value().diagnostics) {
        if (d.rule_id == "blocking-in-ff") {
            found_blocking = true;
            break;
        }
    }
    CHECK(found_blocking);
}

// ========================================================================
// Doc extractor pipeline
// ========================================================================

TEST_CASE("integration: doc extractor on project files", "[integration]") {
    TempDir td;
    td.write_file("fifo.sv", R"(
/// A simple synchronous FIFO.
///
/// @param DEPTH FIFO depth (must be power of 2).
/// @port clk System clock
/// @port data_in Input data bus
module fifo_sync #(
    parameter DEPTH = 16
)(
    input  logic        clk,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out,
    output logic        full
);
endmodule
)");

    auto source = (td.path / "fifo.sv").string();
    std::ifstream f(source);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    auto lex_r = lex(content, source, true);
    REQUIRE(lex_r.is_ok());

    auto parse_r = parse(lex_r.value(), source, true);
    REQUIRE(parse_r.is_ok());

    doc::DocExtractor extractor;
    auto docs = extractor.extract(lex_r.value(), parse_r.value(), source);

    REQUIRE(docs.size() >= 1);
    CHECK(docs[0].name == "fifo_sync");
    CHECK(docs[0].doc.brief.find("FIFO") != std::string::npos);
}

// ========================================================================
// Symbol remapping on parsed files
// ========================================================================

TEST_CASE("integration: SR collision detection on real parsed files", "[integration]") {
    TempDir td;
    td.write_file("ip_a/counter.v",
        "module counter(input clk, output [7:0] count);\n"
        "endmodule\n");
    td.write_file("ip_b/counter.v",
        "module counter(input clk, input rst, output [7:0] count);\n"
        "endmodule\n");

    // Lex + parse both files
    std::unordered_map<std::string, ParseResult> parse_results;

    for (auto& rel : {"ip_a/counter.v", "ip_b/counter.v"}) {
        auto full = (td.path / rel).string();
        std::ifstream f(full);
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto lr = lex(content, full);
        REQUIRE(lr.is_ok());
        auto pr = parse(lr.value(), full);
        REQUIRE(pr.is_ok());
        parse_results[full] = std::move(pr.value());
    }

    // Detect collisions
    auto collisions = SymbolRemapper::detect_collisions(parse_results);
    REQUIRE(collisions.size() == 1);
    CHECK(collisions[0].name == "counter");
    CHECK(collisions[0].origins.size() == 2);

    // Build remap table
    auto table = SymbolRemapper::build_remap_table(collisions);
    CHECK(table.size() == 2);

    // Apply remapping
    for (auto& [file, pr] : parse_results) {
        auto it = table.find(file);
        if (it != table.end()) {
            SymbolRemapper::remap_parse_result(pr, it->second);
        }
    }

    // Verify: names are now unique
    std::string name_a, name_b;
    for (auto& [file, pr] : parse_results) {
        if (file.find("ip_a") != std::string::npos) {
            name_a = pr.units[0].name;
        } else {
            name_b = pr.units[0].name;
        }
    }
    CHECK(name_a != name_b);
    CHECK(name_a.find("counter_") == 0);
    CHECK(name_b.find("counter_") == 0);
}

// ========================================================================
// Local overrides
// ========================================================================

TEST_CASE("integration: local overrides discovery", "[integration]") {
    TempDir td;
    td.write_file("Loom.toml", R"TOML(
[package]
name = "override_test"
version = "0.1.0"
)TOML");

    // No Loom.local: should return empty overrides
    auto ov = discover_local_overrides(td.path);
    REQUIRE(ov.is_ok());
    CHECK(ov.value().empty());

    // Create Loom.local
    td.write_file("Loom.local", R"TOML(
[overrides]
my_dep = { path = "../my_dep" }
)TOML");

    auto ov2 = discover_local_overrides(td.path);
    REQUIRE(ov2.is_ok());
    CHECK(ov2.value().count() == 1);
    CHECK(ov2.value().has_override("my_dep"));
}

// ========================================================================
// Lockfile roundtrip
// ========================================================================

TEST_CASE("integration: lockfile save and load", "[integration]") {
    TempDir td;
    auto lock_path = (td.path / "Loom.lock").string();

    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "test_proj";
    lf.root_version = "1.0.0";

    LockedPackage pkg;
    pkg.name = "dep_a";
    pkg.version = "2.0.0";
    pkg.source = "git+https://github.com/example/dep_a.git";
    pkg.commit = "abc123def456789";
    pkg.ref = "v2.0.0";
    pkg.checksum = "sha256:deadbeef";
    pkg.dependencies = {"dep_b"};
    lf.packages.push_back(std::move(pkg));

    LockedPackage pkg2;
    pkg2.name = "dep_b";
    pkg2.version = "1.5.0";
    pkg2.source = "path+../dep_b";
    lf.packages.push_back(std::move(pkg2));

    REQUIRE(lf.save(lock_path).is_ok());

    auto loaded = LockFile::load(lock_path);
    REQUIRE(loaded.is_ok());
    CHECK(loaded.value().loom_version == "0.1.0");
    CHECK(loaded.value().root_name == "test_proj");
    CHECK(loaded.value().packages.size() == 2);

    auto* found = loaded.value().find("dep_a");
    REQUIRE(found != nullptr);
    CHECK(found->version == "2.0.0");
    CHECK(found->dependencies.size() == 1);
    CHECK(found->dependencies[0] == "dep_b");
}

// ========================================================================
// File locking
// ========================================================================

TEST_CASE("integration: file lock acquire and release", "[integration]") {
    TempDir td;
    auto lock_path = td.path / "test.lock";

    {
        FileLock lock(lock_path);
        CHECK(lock.is_locked());
        CHECK(fs::exists(lock_path));
    }
    // Lock released after scope
}

// ========================================================================
// Signal handling
// ========================================================================

TEST_CASE("integration: signal handler installation", "[integration]") {
    install_signal_handlers();
    CHECK_FALSE(is_interrupted());
    // Can't safely send SIGINT in a test, but verify the API works
    reset_interrupt();
    CHECK_FALSE(is_interrupted());
}
